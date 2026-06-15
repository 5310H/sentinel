#!/bin/bash
# Prepare SPIFFS image with server.pem for ESP32 build
# This script copies server.pem to the build directory for inclusion in SPIFFS partition

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_CERT="$PROJECT_ROOT/server.pem"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
SPIFFS_IMG_DIR="$BUILD_DIR/spiffs_image"

echo "=========================================="
echo "Preparing SPIFFS Image with Certificate"
echo "=========================================="
echo "Source: $SOURCE_CERT"
echo "Destination: $SPIFFS_IMG_DIR"
echo ""

# Check if certificate exists
if [ ! -f "$SOURCE_CERT" ]; then
    echo "❌ ERROR: server.pem not found at $SOURCE_CERT"
    echo ""
    echo "To generate a certificate, run:"
    echo "  ./scripts/generate_long_cert.sh"
    echo ""
    exit 1
fi

# Create SPIFFS image directory
mkdir -p "$SPIFFS_IMG_DIR"

# Copy certificate
cp "$SOURCE_CERT" "$SPIFFS_IMG_DIR/server.pem"

# Verify
if [ -f "$SPIFFS_IMG_DIR/server.pem" ]; then
    SIZE=$(stat -f%z "$SPIFFS_IMG_DIR/server.pem" 2>/dev/null || stat -c%s "$SPIFFS_IMG_DIR/server.pem" 2>/dev/null || echo "unknown")
    echo "✓ Certificate copied to SPIFFS image directory"
    echo "  Size: $SIZE bytes"
    echo ""
    echo "=========================================="
    echo "SPIFFS Image Ready"
    echo "=========================================="
    echo "Run 'idf.py build' to include certificate in firmware"
else
    echo "❌ ERROR: Failed to copy certificate"
    exit 1
fi
