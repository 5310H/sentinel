#!/usr/bin/env python3
"""
ESPHome Entity Key Guesser
Tests common entity key values to find the correct ones
"""

import socket
import struct
import time
import sys

def decode_varint(data, pos=0):
    result = 0
    shift = 0
    while True:
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        if not (byte & 0x80):
            break
        shift += 7
    return result, pos

def test_entity_key(hostname, port, password, encryption_key, test_key):
    """Test if a specific entity key exists on the device"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        sock.connect((hostname, port))

        # Send Hello
        hello = b'\x00\x0bHello' + b'\x00' * 9
        sock.send(hello)
        hello_resp = sock.recv(1024)

        # Send Connect (skip auth for now)
        connect = b'\x01\x00'
        sock.send(connect)
        connect_resp = sock.recv(1024)

        # Send SubscribeStates for specific entity
        # This should tell us if the entity exists
        subscribe_msg = b'\x1c'  # SubscribeStates type
        # Add entity key as varint
        key_bytes = b''
        key = test_key
        while key > 0:
            byte = key & 0x7F
            key >>= 7
            if key > 0:
                byte |= 0x80
            key_bytes += bytes([byte])

        subscribe_msg += key_bytes
        sock.send(subscribe_msg)

        # Try to read response
        try:
            response = sock.recv(1024)
            if response:
                return True  # Entity might exist
        except socket.timeout:
            pass

        sock.close()
        return False

    except Exception as e:
        return False

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 guess_entity_keys.py <hostname> <password>")
        sys.exit(1)

    hostname = sys.argv[1]
    password = sys.argv[2] if len(sys.argv) > 2 else ""

    print(f"Testing common entity keys on {hostname}...")

    # Common entity key ranges to test
    common_keys = [1, 2, 3, 4, 5, 10, 11, 12, 13, 14, 15, 20, 21, 22, 23, 24, 25,
                   100, 101, 102, 103, 104, 105, 1000, 1001, 1002, 1003, 1004, 1005]

    found_keys = []

    for key in common_keys:
        if test_entity_key(hostname, 6053, password, "", key):
            found_keys.append(key)
            print(f"✓ Found entity with key: {key}")
        else:
            print(f"✗ Key {key}: not found")

        time.sleep(0.1)  # Small delay between tests

    print(f"\nResults for {hostname}:")
    if found_keys:
        print(f"Found {len(found_keys)} potential entity keys: {found_keys}")
        print("Try these values in your esphome.json configuration")
    else:
        print("No common entity keys found. The device might use different keys.")

if __name__ == "__main__":
    main()