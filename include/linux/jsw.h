#pragma once

#include <linux/mm.h>

// Call this to get the memory pool for network buffers allocated in guest
// Returns NULL on allocation failure
void *alloc_jsw(unsigned long size);

// Call this to free the memory pool obtained by `alloc_jsw`
void free_jsw(void *ptr);
