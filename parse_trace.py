#!/usr/bin/env python3
"""
Parse NETMEM trace output and track memory allocations.

For STRUCT_* entries: track by skb field
For DATA_* entries: track by head field
"""

import re
import sys

def parse_trace_file(filename):
    """Parse the trace file and accumulate deltas by key."""
    struct_totals = {}  # skb -> total delta
    data_totals = {}    # head -> total delta

    # Pattern to match trace lines
    pattern = r'\[OP \d+\] \[(\w+)\] skb=(\w+) head=(\w+) func=\S+ delta=(-?\d+)'

    with open(filename, 'r') as f:
        for line in f:
            match = re.search(pattern, line)
            if match:
                operation = match.group(1)
                skb = match.group(2)
                head = match.group(3)
                delta = int(match.group(4))

                # For STRUCT_* operations, use skb as key
                if operation.startswith('STRUCT_'):
                    if skb not in struct_totals:
                        struct_totals[skb] = 0
                    struct_totals[skb] += delta

                # For DATA_* operations, use head as key
                elif operation.startswith('DATA_'):
                    if head not in data_totals:
                        data_totals[head] = 0
                    data_totals[head] += delta

    return struct_totals, data_totals

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <trace-file>")
        sys.exit(1)

    filename = sys.argv[1]

    struct_totals, data_totals = parse_trace_file(filename)

    print("STRUCT totals (by skb):")
    print("-" * 60)
    for skb, total in sorted(struct_totals.items()):
        print(f"  skb={skb}: {total:>10}")

    print("\nDATA totals (by head):")
    print("-" * 60)
    for head, total in sorted(data_totals.items()):
        print(f"  head={head}: {total:>10}")

    print("\nSummary:")
    print("-" * 60)
    print(f"Total STRUCT delta: {sum(struct_totals.values())}")
    print(f"Total DATA delta:   {sum(data_totals.values())}")

if __name__ == "__main__":
    main()

