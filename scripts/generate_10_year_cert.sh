#!/bin/bash
# This script generates a self-signed SSL certificate valid for 10 years.
# The certificate and private key are saved to `data/server.pem`.

# Create the data directory if it doesn't exist
mkdir -p data

openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -keyout data/server.pem -out data/server.pem -subj "/CN=sentinel-esp"

echo "10-year certificate created at data/server.pem"
