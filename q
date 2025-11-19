#!/bin/bash
# based on: https://github.com/fomichev/dotfiles/blob/master/bin/q
# based on: https://github.com/cs423-uiuc/q-script/blob/main/cs423-q

# Note:
#
# Host machine has to have `resize` program installed
#
#  sudo apt install xterm
#

# Todo:
#
# <del>1. Optionally do not require /dev/kvm permissions</del>
# 2. Verify if this script works under host /mnt/ directory

IS_GUEST=${IS_GUEST:-false}

if $IS_GUEST; then
    # To-do: Configure by directly modifying the below lines
    GUEST_HOSTNAME=${GUEST_HOSTNAME:-guest}
else
    # To-do: Configure by directly modifying the below lines
    VM_NUM_CPU=${VM_NUM_CPU:-2}
    VM_MEMORY=${VM_MEMORY:-8192}
    # Configure by command line arguments
    ENABLE_GDB=""
    VM_COMMANDS=""
    # Auto-detected in "main"
    VM_ARCH=""
fi

usage() {
    if [[ -n "$*" ]]; then
        echo "error: $@"
        echo
    fi

    echo "q [OPTION]"
    echo
    echo "Run it from the kernel build directory (make sure .config is there)"
    echo
    echo "OPTION:"
    echo "    g          - support being attached with gdb"
    echo "    c COMMANDS - run COMMANDS instead of interactive bash"
    echo
    exit 0
}

# This function is called _BEFORE_ QEMU starts (on host).
host() {
    local kernel_image="$1"

    [[ -e ".config" ]] || usage

    # Somehow for kernel 4.x, 5.4.y, 5.10.y etc this mount tag has to be /dev/root

    local kernel_major_version
    local kernel_minor_version
    local rootfs_mount_tag

    kernel_major_version=$( grep 'VERSION = [0-9]' Makefile |\
                            rev | cut -d ' ' -f 1 | rev )
    kernel_minor_version=$( grep 'PATCHLEVEL = [0-9]' Makefile |\
                            rev | cut -d ' ' -f 1 | rev )
    if [ "${kernel_major_version}" -le "4" ]; then
        rootfs_mount_tag="/dev/root"
    elif [ "${kernel_major_version}" -eq "5" ]; then
        if [[ "${kernel_minor_version}" -le "15" ]]; then
            rootfs_mount_tag="/dev/root"
        fi
    else
        rootfs_mount_tag="9p-root"
    fi

    local cmdline
    local fs

    # Root:
    #   9p --> overlay, changes within the guest will NOT be reflected outside
    # Except kernel source directory:
    #   9p, changes within the guest WILL be reflected outside
    fs+=" -fsdev local,multidevs=remap,id=vfs1,path=/,security_model=none,readonly=on"
    fs+=" -fsdev local,id=vfs2,path=$(pwd),security_model=none"
    fs+=" -device virtio-9p-pci,fsdev=vfs1,mount_tag=$rootfs_mount_tag"
    fs+=" -device virtio-9p-pci,fsdev=vfs2,mount_tag=9p-kernel-src"

    local console
    console+=" -display none"
    console+=" -serial mon:stdio"

    local networking
    networking+=" -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:8080"
    networking+=" -device virtio-net-pci,netdev=net0"

    cmdline+=" earlyprintk=serial,ttyS0,115200"

    if [[ "${VM_ARCH}" != "arm64" ]]; then
        cmdline+=" console=ttyS0"
        cmdline+=" kgdboc=ttyS1,115200"
    fi
    cmdline+=" oops=panic retbleed=off"

    cmdline+=" root=$rootfs_mount_tag"
    cmdline+=" rootfstype=9p"
    cmdline+=" rootflags=version=9p2000.L,trans=virtio,access=any"
    cmdline+=" ro"
    cmdline+=" nokaslr"

    local gdb
    $ENABLE_GDB && gdb+=" -s"

    local accel
    if [[ "$(uname -m)" = "${VM_ARCH}" ]]; then
        if [[ -e /dev/kvm && -r /dev/kvm && -w /dev/kvm ]]; then
            accel+=" -machine accel=kvm:tcg"
            accel+=" -enable-kvm"
        fi
    fi

    local cpu
    local qemu_suffix
    local tty

    case "${VM_ARCH}" in
    x86_64)
        if [[ "$(uname -m)" = "${VM_ARCH}" ]]; then
            if [[ -e /dev/kvm && -r /dev/kvm && -w /dev/kvm ]]; then
                cpu="host"
            else
                cpu="max"
            fi
        fi
        qemu_suffix=x86_64
        tty=ttyS0
        ;;
    arm64)
        if [[ "$(uname -m)" = "${VM_ARCH}" ]]; then
            cpu="host"
        else
            accel+=" -machine virt -accel tcg "
            cpu="max"
        fi
        qemu_suffix=aarch64
        tty=ttyAMA0
        ;;
    esac

    local init

    init+="IS_GUEST='true' "
    init+="GUEST_TTY='$tty' "
    init+="GUEST_HOME='$HOME' "
    init+="GUEST_KERNEL_SRC_DIR='$(pwd)' "
    init+="GUEST_COMMANDS='$VM_COMMANDS' "
    init+=" $(realpath $0) "

    cmdline+=" init=/bin/sh -- -c \"$init\""

    # ???
    qemu-system-${qemu_suffix} \
        -nographic \
        -no-reboot \
        $accel \
        -device i6300esb,id=watchdog0 \
        -watchdog-action pause \
        -device virtio-rng-pci \
        -cpu $cpu \
        -smp $VM_NUM_CPU \
        -m $VM_MEMORY \
        $fs \
        $console \
        $networking \
        $gdb \
        -kernel "$kernel_image" \
        -append "$cmdline"
}

# FIXME ANSI color code may interleave with kernel messages and eventually
# corrupt the rest of this session.
say() {
    printf "\33[32m"
    echo ">" "$@"
    printf "\33(B\33[m"
}

# This function is called _AFTER_ QEMU starts (within guest).
guest() {
    # At this point, what the guest sees by `findmnt` is the same as the host
    say "(oldroot) mount /proc"
    mount -n -t proc -o nosuid,noexec,nodev proc /proc
    # After mounting proc, 9p takes effect:
    #
    # TARGET  SOURCE    FSTYPE   OPTIONS
    # /       9p-root   9p       ro,...
    # |-/dev  devtmpfs  devtmpfs
    # `-/proc proc      proc

    # Create a writable tmpfs before configuring overlay. The mount point should
    # exist on the host.
    say "(oldroot) mount /tmp"
    mount -n -t tmpfs tmpfs /tmp

    say "(oldroot) configure overlay"
    local overlay=/tmp/rootdir-overlay
    mkdir -p $overlay/{lower,upper,work,newroot}
    mount --bind / $overlay/lower
    mount -t overlay overlay \
          -o lowerdir=$overlay/lower,upperdir=$overlay/upper,workdir=$overlay/work \
          $overlay/newroot
    # TARGET  SOURCE   FSTYPE  OPTIONS
    # /       9p-root  9p      ro,...
    # |-/dev  devtmpfs devtmpf
    # |-/proc proc     proc
    # `-/tmp  tmpfs    tmpfs   rw,...
    #   |-/tmp/rootdir-overlay/lower
    #   |     9p-root  9p      ro,...
    #   `-/tmp/rootdir-overlay/newroot
    #         overlay  overlay rw,...

    say pivot_root
    mkdir -p $overlay/newroot/oldroot
    # pivot_root: let the 1st arg be the new root and move the old root to the
    # 2nd arg. At this point, what the guest sees by `findmnt` is the same as
    # the host.
    pivot_root $overlay/newroot $overlay/newroot/oldroot

    say mount /proc
    mount -n -t proc -o nosuid,noexec,nodev proc /proc
    # After mounting proc, ealier settings within guest take effect again:
    #
    # TARGET            SOURCE   FSTYPE  OPTIONS
    # /                 overlay  overlay rw,...
    # |-/oldroot        9p-root  9p      ro,...
    # | |-/oldroot/dev  devtmpfs devtmpf
    # | |-/oldroot/proc proc     proc
    # | `-/oldroot/tmp  tmpfs    tmpfs   rw,...
    # |   `-/oldroot/tmp/rootdir-overlay/lower
    # |                 9p-root  9p      ro,...
    # `-/proc           proc     proc
    say umount no longer useful fs
    umount /oldroot/tmp/rootdir-overlay/lower
    umount /oldroot/tmp
    umount /oldroot/proc
    umount /oldroot/dev

    say move oldroot under /mnt
    mkdir /mnt/host-root-readonly
    mount --move /oldroot /mnt/host-root-readonly
    # TARGET  SOURCE  FSTYPE  OPTIONS
    # /       overlay overlay rw,...
    # |-/mnt/host-root-readonly
    # |       9p-root 9p      ro,...
    # `-/proc proc    proc

    say empty /etc/fstab
    >/etc/fstab

    say mount /sys, /tmp, /run, /dev
    # /sys
    mount -n -t sysfs -o nosuid,noexec,nodev sys /sys
    # TODO grep for the following pattern and decide whether to mount or not
    #
    #   CONFIG_DEBUG_FS=y
    #   CONFIG_CONFIGFS_FS=y
    #
    mount -n -t configfs configfs /sys/kernel/config
    mount -n -t debugfs debugfs /sys/kernel/debug
    if [[ -d /sys/kernel/security ]]; then
        mount -n -t securityfs security /sys/kernel/security
    fi
    # /tmp
    mount -n -t tmpfs tmpfs /tmp
    # /run
    mount -n -t tmpfs tmpfs /run
    # /dev
    mount -n -t devtmpfs -o mode=0755,nosuid,noexec devtmpfs /dev
    mkdir -p -m 0755 /dev/shm /dev/pts
    mount -n -t devpts -o gid=tty,mode=620,noexec,nosuid devpts /dev/pts
    mount -n -t tmpfs -o mode=1777,nosuid,nodev tmpfs /dev/shm

    say mount kernel source directory
    mount -n -t 9p -o trans=virtio 9p-kernel-src "$GUEST_KERNEL_SRC_DIR"

    say configure networking
    # Bring up loopback interface
    ip link set dev lo up
    # Configure the network interface (usually eth0)
    if [[ -d /sys/class/net ]]; then
        for iface in /sys/class/net/*; do
            iface_name=$(basename "$iface")
            if [[ "$iface_name" != "lo" ]]; then
                say "configuring interface $iface_name"
                ip link set dev "$iface_name" up
                # Use DHCP to get IP address
                if command -v udhcpc >/dev/null 2>&1; then
                    udhcpc -i "$iface_name" -q
                elif command -v dhclient >/dev/null 2>&1; then
                    dhclient "$iface_name"
                else
                    # Fallback: configure static IP for user mode networking
                    ip addr add 10.0.2.15/24 dev "$iface_name"
                    ip route add default via 10.0.2.2 dev "$iface_name"
                fi
                break
            fi
        done
    fi

    say "mount /boot (as a tmpfs) and put current kernel config there"
    local kernel_version="$(uname -r)"
    mount -n -t tmpfs tmpfs /boot
    ln -s "$GUEST_KERNEL_SRC_DIR/.config" /boot/config-$kernel_version

    say configure \$HOME
    export HOME=$GUEST_HOME

    say configure hostname
    hostname "$GUEST_HOSTNAME"
    echo "$GUEST_HOSTNAME" > /etc/hostname

    say configure DNS
    # Configure DNS for user mode networking
    echo "nameserver 10.0.2.3" > /etc/resolv.conf
    echo "nameserver 8.8.8.8" >> /etc/resolv.conf
    echo "nameserver 8.8.4.4" >> /etc/resolv.conf

    say configure bash environmnet

    # Clear environment variables only used in this q script

    local tty=$GUEST_TTY
    local kernel_src_dir=$GUEST_KERNEL_SRC_DIR
    local commands=$GUEST_COMMANDS

    unset IS_GUEST
    # unset GUEST_HOSTNAME
    unset GUEST_TTY
    unset GUEST_HOME
    unset GUEST_KERNEL_SRC_DIR
    unset GUEST_COMMANDS

    # All host environment variables from the host will be lost except those set
    # in ~/.bashrc

    >/etc/profile
    >/etc/bash.bashrc

    local rcfile=/tmp/.bashrc

    cat << EOF > $rcfile
source \$HOME/.bashrc
# Terminal color and width etc
resize &> /dev/null

# Networking helpers
alias netinfo='ip addr show && echo "---" && ip route show && echo "---" && cat /etc/resolv.conf'
alias pingtest='ping -c 3 8.8.8.8'
alias wgettest='wget -q --spider http://www.google.com && echo "Internet connectivity OK" || echo "No internet connectivity"'
alias pinghost='ping -c 3 10.0.2.2 && echo "Host reachable" || echo "Host not reachable"'
alias hostinfo='echo "Host IP: 10.0.2.2" && echo "Guest can access host services on this IP" && echo "Example: curl http://10.0.2.2:8080"'
alias iperfhelp='echo "=== iperf between guest and host ===" && echo "1. On HOST: iperf3 -s (start server)" && echo "2. On GUEST: iperf3 -c 10.0.2.2 (connect to host)" && echo "3. For reverse test: iperf3 -c 10.0.2.2 -R" && echo "4. Check if iperf3 is installed: which iperf3"'

echo "Networking configured. Use 'netinfo' to check network status, 'pingtest' to test connectivity."
echo "Host is accessible at 10.0.2.2 - use 'pinghost' to test, 'hostinfo' for details."
echo "For iperf testing, use 'iperfhelp' for instructions."
EOF

    cd $kernel_src_dir

    if [[ -n "$commands" ]]; then
        say "non-interactive bash command(s)"
        setsid bash --rcfile $rcfile -c "$commands"
        if [[ ! $? -eq 0 ]]; then
            say "command(s) failed, starting interactive bash"
            setsid bash --rcfile $rcfile 0<>"/dev/$tty" 1>&0 2>&0
        #   ^^^^^^                       ^^^^^^^^^^^^^^ ^^^^ ^^^^ ???
        fi
    else
        say interactive bash with rcfile $rcfile
        setsid bash --rcfile $rcfile 0<>"/dev/$tty" 1>&0 2>&0
    fi

    echo
    say poweroff
    # TODO relies on CONFIG_MAGIC_SYSRQ=y
    echo o >/proc/sysrq-trigger
    sleep 30
}

# main() {

    if $IS_GUEST; then
        guest
        exit 0
    fi

    while getopts "hgc:" opt; do
        case $opt in
        h) usage ;;
        g) ENABLE_GDB=true ;;
        c) VM_COMMANDS="$OPTARG" ;;
        esac
    done
    shift $((OPTIND - 1))

    if [[ -e "arch/x86/boot/bzImage" ]]; then
        kernel_image="arch/x86/boot/bzImage"
    elif [[ -e "arch/arm64/boot/Image" ]]; then
        kernel_image="arch/arm64/boot/Image"
    fi

    [ -n "$kernel_image" ] || usage "kernel image not found or not supported"

    if file $kernel_image | grep -q x86; then
        VM_ARCH=x86_64
    elif file $kernel_image | grep -q ARM64; then
        VM_ARCH=arm64
    fi

    [ -n "$VM_ARCH" ] || usage "kernel architecture not supported"

    host "$kernel_image"

# }
