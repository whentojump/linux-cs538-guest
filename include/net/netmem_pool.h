#ifndef _NET_NETMEM_POOL_H
#define _NET_NETMEM_POOL_H

#include <linux/types.h>
#include <linux/gfp.h>

#define NETMEM_FROM_POOL	0xDEADBEEF
#define NETMEM_FROM_KMALLOC	0xCAFEBABE

struct netmem_alloc_header {
	u32 magic;	// NETMEM_FROM_POOL or NETMEM_FROM_KMALLOC
	u32 size;
};

int netmem_pool_init(void);
void netmem_pool_cleanup(void);

void *netmem_pool_alloc(size_t size, gfp_t gfp);
void netmem_pool_free(void *ptr);

size_t netmem_pool_available(void);
size_t netmem_pool_size_get(void);

#endif /* _NET_NETMEM_POOL_H */
