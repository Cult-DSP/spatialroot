#!/usr/bin/env bash
# command line tool to download basic example files

set -euo pipefail

FILE_ID_1="16Z73gODkZzCWjYy313FZc6ScG-CCXL4h"
OUTPUT_NAME_1="driveExample1.wav"
FILE_ID_2="1-oh0tixJV3C-odKdcM7Ak-ziCv5bNKJB"
OUTPUT_NAME_2="driveExample2.wav"
FILE_ID_3="1NsW8xj4wFGhGtSKRIuPL4E2yoEJEIq4X"
OUTPUT_NAME_3="driveExampleSpruce.wav"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DATA_DIR="$SCRIPT_DIR/sourceData"

download_from_google_drive() {
    local file_id="$1"
    local output_name="$2"
    local output_path="$SOURCE_DATA_DIR/$output_name"
    local url="https://drive.usercontent.google.com/download?id=${file_id}&export=download&confirm=t"

    mkdir -p "$SOURCE_DATA_DIR"

    echo ""
    echo "Downloading example file to: $output_path"
    echo "This may take a while for large files..."
    echo ""

    curl -L "$url" -o "$output_path"

    echo ""
    echo "Download complete!"
    echo "Saved to: $output_path"

    if [[ -f "$output_path" ]]; then
        local size_mb
        size_mb=$(du -m "$output_path" | cut -f1)
        echo "File verified: ${size_mb} MB"
    else
        echo "WARNING: File not found at $output_path"
    fi
}

download_from_google_drive "$FILE_ID_1" "$OUTPUT_NAME_1"
download_from_google_drive "$FILE_ID_2" "$OUTPUT_NAME_2"
download_from_google_drive "$FILE_ID_3" "$OUTPUT_NAME_3"
