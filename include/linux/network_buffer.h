/*
 * Network Buffer Management
 *
 * This header defines the interface for managing a dedicated network buffer
 * that can be used for network communication and shared with hypervisors.
 *
 * Copyright (C) 2024
 */

#ifndef _LINUX_NETWORK_BUFFER_H
#define _LINUX_NETWORK_BUFFER_H

#include <asm/page.h>

/* Network buffer configuration */
#define NETWORK_BUFFER_SIZE		(16ULL * 1024 * 1024 * 1024)  /* 16GB */ // TODO
#define NETWORK_BUFFER_ALIGN		PAGE_SIZE
#define NETWORK_BUFFER_CHUNK_SIZE	(16 * 1024 * 1024)  /* 16MB chunks */ // TODO

/* Hypervisor notification API */
void notify_network_buffer_addr(phys_addr_t start, phys_addr_t end);

/* Network buffer management */
struct network_buffer_info {
	phys_addr_t start;
	phys_addr_t end;
	void *virt_addr;
	size_t size;
	bool initialized;
};

#endif /* _LINUX_NETWORK_BUFFER_H */
