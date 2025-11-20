#!/bin/bash

# Sort by the first column (skbuff->head) using an external script

f=$(mktemp)

cat /proc/netmem_stats/per_site > $f

head -2 $f
tail -n+3 $f | head -n-1 | sort
tail -1 $f

rm -f $f

grep 'Total Bytes' /proc/netmem_stats/pool
