#!/bin/bash

# Usage: ./clean_files.sh file1 file2 file3 ...
# Processes each file in place.

if [ $# -eq 0 ]; then
    echo "Usage: $0 file1 [file2 ...]"
    exit 1
fi

for file in "$@"; do
    if [ ! -f "$file" ]; then
        echo "Error: File not found: $file"
        continue
    fi

    # Step 1: Remove \r (handles \r\n -> \n)
    # Step 2: Replace tabs with 4 spaces
    # Step 3: Remove trailing whitespace
    # All done in place with sed and expand
    sed -i $'s/\r$//' "$file"          # Remove any trailing \r
    expand -t 4 --initial "$file" |   # Expand tabs to 4 spaces (no leading tabs issue for replacement)
    sed -E $'s/[ \t]+$//' > "${file}.tmp"  # Remove trailing spaces/tabs (though tabs already expanded)

    mv "${file}.tmp" "$file"
    echo "Processed: $file"
done
