import sys

def main():
    if len(sys.argv) < 2:
        print(f"Usage: python {sys.argv[0]} <filename>")
        sys.exit(1)

    filename = sys.argv[1]

    try:
        with open(filename, 'r') as f:
            lines = f.readlines()

        for i, line in enumerate(lines):
            if "dump_stack_" in line:
                # Print the line and the next two (if available)
                print("".join(lines[i:i+3]).rstrip())  # rstrip() removes extra blank line
                print("-" * 40)  # separator for readability (optional)

    except FileNotFoundError:
        print(f"Error: file '{filename}' not found.")
        sys.exit(1)

if __name__ == "__main__":
    main()
