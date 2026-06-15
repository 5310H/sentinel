#!/usr/bin/env python3
"""
ESPHome Entity Discovery Tool
Finds entity keys from ESPHome devices
"""

import socket
import struct
import time
import sys

def decode_varint(data, pos=0):
    """Decode a protobuf varint"""
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

def discover_entities(hostname, port=6053, password=None):
    """Connect to ESPHome device and discover entities"""
    print(f"\n=== Connecting to {hostname}:{port} ===")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect((hostname, port))
        print("✓ Connected successfully")

        # Send Hello
        hello_msg = b'\x00\x0bHello\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00'
        sock.send(hello_msg)

        # Read HelloResponse
        response = sock.recv(1024)
        if len(response) < 4:
            print("✗ No HelloResponse received")
            return

        msg_type = response[0]
        msg_len = struct.unpack('<I', response[1:5])[0]
        print(f"✓ Received HelloResponse (type: {msg_type}, len: {msg_len})")

        # Send Connect (no auth for now)
        connect_msg = b'\x01\x00'
        sock.send(connect_msg)

        # Read ConnectResponse
        response = sock.recv(1024)
        if len(response) < 4:
            print("✗ No ConnectResponse received")
            return

        msg_type = response[0]
        msg_len = struct.unpack('<I', response[1:5])[0]
        print(f"✓ Received ConnectResponse (type: {msg_type}, len: {msg_len})")

        # Send ListEntities
        list_msg = b'\x0b\x00'
        sock.send(list_msg)

        print("\n=== Discovering Entities ===")

        entities_found = 0
        switches = []
        sensors = []

        while True:
            try:
                response = sock.recv(2048)
                if not response or len(response) < 4:
                    break

                msg_type = response[0]
                msg_len = struct.unpack('<I', response[1:5])[0]

                if msg_type == 12:  # LIST_ENTITIES_SWITCH
                    entity = parse_entity(response[5:5+msg_len], "SWITCH")
                    if entity:
                        switches.append(entity)
                        entities_found += 1
                        print(f"🔌 SWITCH: {entity['name']} (key: {entity['key']})")

                elif msg_type == 13:  # LIST_ENTITIES_BINARY_SENSOR
                    entity = parse_entity(response[5:5+msg_len], "BINARY_SENSOR")
                    if entity:
                        sensors.append(entity)
                        entities_found += 1
                        print(f"👁️  SENSOR: {entity['name']} (key: {entity['key']})")

                elif msg_type == 19:  # LIST_ENTITIES_DONE
                    print("✓ Entity discovery complete")
                    break

            except socket.timeout:
                break

        sock.close()

        print(f"\n=== Summary for {hostname} ===")
        print(f"Found {entities_found} entities total")
        print(f"Switches: {len(switches)}")
        print(f"Binary Sensors: {len(sensors)}")

        if switches:
            print("\nSWITCHES:")
            for sw in switches:
                print(f"  - {sw['name']}: entity_key = {sw['key']}")

        if sensors:
            print("\nBINARY SENSORS:")
            for sen in sensors:
                print(f"  - {sen['name']}: entity_key = {sen['key']}")

        return switches, sensors

    except Exception as e:
        print(f"✗ Error: {e}")
        return [], []

def parse_entity(data, entity_type):
    """Parse entity from protobuf data"""
    entity = {'type': entity_type, 'name': '', 'object_id': '', 'key': 0}

    pos = 0
    while pos < len(data):
        try:
            field_num, pos = decode_varint(data, pos)
            wire_type = field_num & 0x07
            field_num >>= 3

            if field_num == 1:  # object_id
                if wire_type == 2:  # length-delimited
                    str_len, pos = decode_varint(data, pos)
                    entity['object_id'] = data[pos:pos+str_len].decode('utf-8', errors='ignore')
                    pos += str_len

            elif field_num == 2:  # name
                if wire_type == 2:  # length-delimited
                    str_len, pos = decode_varint(data, pos)
                    entity['name'] = data[pos:pos+str_len].decode('utf-8', errors='ignore')
                    pos += str_len

            elif field_num == 5:  # key
                if wire_type == 0:  # varint
                    entity['key'], pos = decode_varint(data, pos)

            else:
                # Skip unknown fields
                if wire_type == 0:  # varint
                    _, pos = decode_varint(data, pos)
                elif wire_type == 2:  # length-delimited
                    str_len, pos = decode_varint(data, pos)
                    pos += str_len
                else:
                    break

        except:
            break

    return entity if entity['name'] and entity['key'] else None

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 discover_entities.py <hostname> [port]")
        print("Example: python3 discover_entities.py 192.168.69.209 6053")
        sys.exit(1)

    hostname = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 6053

    switches, sensors = discover_entities(hostname, port)

    if not switches and not sensors:
        print(f"\n❌ No entities found on {hostname}")
        print("\nTroubleshooting:")
        print("1. Make sure the ESPHome device is running and accessible")
        print("2. Check that the device has switches or binary sensors configured")
        print("3. Verify the IP address and port are correct")
        print("4. Try: ping " + hostname)
    else:
        print("\n✅ Success! Update your esphome.json with these entity_key values")
        print("Example:")
        print('{')
        print('  "hostname": "' + hostname + '",')
        print('  "entity_key": ' + str((switches + sensors)[0]['key']) + '  // ← Use this value')
        print('}')

if __name__ == "__main__":
    main()