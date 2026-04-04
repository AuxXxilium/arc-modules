#!/bin/bash

# Script to strip all .ko files in subdirectories

# Check if the user has provided a directory
if [ -z "$1" ]; then
  echo "Usage: $0 <directory>"
  exit 1
fi

# Base directory
BASE_DIR="$1"

# Find and strip all .ko files
find "$BASE_DIR" -type f -name "*.ko" | while read -r ko_file; do
  echo "Stripping: $ko_file"
  strip --strip-unneeded "$ko_file"
done

echo "All .ko files have been stripped."