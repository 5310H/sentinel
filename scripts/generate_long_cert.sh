#!/bin/bash
# Generate long-lived self-signed certificate for ESP32 Sentinel system
# This certificate will be valid for 10 years (3650 days)

set -e

DAYS=3650  # 10 years
KEY_SIZE=2048
CN="Sentinel-ESP"
OUTPUT_DIR="${1:-.}"
OUTPUT="$OUTPUT_DIR/server.pem"

echo "=========================================="
echo "Generating Long-Lived Certificate"
echo "=========================================="
echo "Valid for: $DAYS days (10 years)"
echo "Output: $OUTPUT"
echo ""

# Generate private key and certificate
echo "Generating RSA $KEY_SIZE-bit key and certificate..."
openssl req -x509 -newkey rsa:$KEY_SIZE \
  -keyout /tmp/server.key \
  -out /tmp/server.crt \
  -days $DAYS \
  -nodes \
  -subj "/CN=$CN/O=Home Security System/C=US/ST=OH/L=Cleveland"

# Combine into PEM format (mongoose expects this format)
echo "Combining certificate and key..."
cat /tmp/server.crt /tmp/server.key > "$OUTPUT"

# Clean up temp files
rm /tmp/server.key /tmp/server.crt

# Display certificate info
echo ""
echo "=========================================="
echo "Certificate Generated Successfully"
echo "=========================================="
echo "File: $OUTPUT"
echo ""
echo "Certificate Details:"
openssl x509 -in "$OUTPUT" -noout -subject -issuer -dates
echo ""
echo "Valid until:"
openssl x509 -in "$OUTPUT" -noout -enddate | cut -d= -f2
echo ""
echo "To verify later:"
echo "  openssl x509 -in $OUTPUT -noout -enddate"
echo ""
echo "=========================================="
