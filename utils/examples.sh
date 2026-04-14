#!/usr/bin/env bash
# Downloads basic example files from HuggingFace

set -euo pipefail

HF_BASE="https://huggingface.co/datasets/lucianparisi/atmos-data/resolve/main"

URL_1="$HF_BASE/driveExample1.wav"
OUTPUT_NAME_1="Each_Example_1.wav"
URL_2="$HF_BASE/driveExample2.wav"
OUTPUT_NAME_2="Echo_Example_2.wav"
URL_3="$HF_BASE/SWALE-ATMOS-LFE.wav"
OUTPUT_NAME_3="LucianParisi_Swale_Atmos_Mix.wav"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DATA_DIR="$SCRIPT_DIR/sourceData"

download_from_hf() {
    local url="$1"
    local output_name="$2"
    local output_path="$SOURCE_DATA_DIR/$output_name"

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

download_from_hf "$URL_1" "$OUTPUT_NAME_1"
download_from_hf "$URL_2" "$OUTPUT_NAME_2"
download_from_hf "$URL_3" "$OUTPUT_NAME_3"
