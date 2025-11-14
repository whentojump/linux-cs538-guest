#!/bin/bash

# Sort by the first column (skbuff->head) using an external script

f=/proc/netmem_stats/per_site

head -3 $f
tail -n+4 $f | head -n-1 | sort
tail -1 $f
