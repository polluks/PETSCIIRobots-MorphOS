#!/bin/sh
# Decompress all gzip'd assets for the MorphOS port
# Run this once before building

echo "Decompressing assets..."

cd Amiga/Data
for f in *.gz; do
    if [ -f "$f" ]; then
        gunzip -f "$f"
        echo "  Decompressed: $f"
    fi
done
cd ../..

echo "Done. Assets are ready."
