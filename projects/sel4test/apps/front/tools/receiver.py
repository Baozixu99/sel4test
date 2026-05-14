#!/usr/bin/env python3
import socket
import struct
import argparse
import os
import sys

# ForwardHeader 格式:
# uint32_t magic;         /* 0x48465744 ("HFWD") */
# uint8_t  service_id;    
# uint8_t  is_bulk;       
# uint16_t reserved;      
# uint32_t total_len;     
HEADER_MAGIC = 0x48465744
HEADER_FORMAT = '<IBBHI'
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

def main():
    parser = argparse.ArgumentParser(description='HyperAMP UDP Receiver with ForwardHeader Parsing')
    parser.add_argument('--port', type=int, required=True, help='UDP port to listen on (e.g., 8888 or 8889)')
    parser.add_argument('--dir', type=str, required=True, help='Directory to save received files')
    args = parser.parse_args()

    if not os.path.exists(args.dir):
        os.makedirs(args.dir)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', args.port))
    
    print(f"[*] Listening on UDP port {args.port}, saving files to {args.dir}")

    file_seq = 0
    current_file = None
    expected_len = 0
    received_len = 0
    service_id = 0
    is_bulk = 0

    while True:
        data, addr = sock.recvfrom(65535)
        if not data:
            continue

        offset = 0
        
        # Check if this packet contains a ForwardHeader
        if current_file is None and len(data) >= HEADER_SIZE:
            magic, s_id, bulk, _, t_len = struct.unpack(HEADER_FORMAT, data[:HEADER_SIZE])
            if magic == HEADER_MAGIC:
                file_seq += 1
                service_id = s_id
                is_bulk = bulk
                expected_len = t_len
                received_len = 0
                
                # Determine file name based on service type and port
                srv_name = "enc" if service_id == 1 else f"srv{service_id}"
                bulk_str = "bulk" if is_bulk else "norm"
                ext = ".txt" if args.port == 8888 else ".png"
                filename = f"fwd_{file_seq:03d}_{srv_name}_{bulk_str}{ext}"
                filepath = os.path.join(args.dir, filename)
                
                print(f"[+] 收到新传输头: service={service_id}, bulk={is_bulk}, total_len={expected_len}")
                print(f"[+] 创建文件: {filename}")
                
                current_file = open(filepath, 'wb')
                offset = HEADER_SIZE
                
                # Special case: expected length is 0 (just a header, no payload)
                if expected_len == 0:
                    print(f"[-] 传输完成 (0 bytes)")
                    current_file.close()
                    current_file = None
                    continue

        if current_file is not None:
            # We are writing to a file
            payload = data[offset:]
            if payload:
                # Truncate if we received more than expected (shouldn't happen typically, but just in case)
                remaining = expected_len - received_len
                if len(payload) > remaining:
                    payload = payload[:remaining]
                    
                current_file.write(payload)
                received_len += len(payload)
                
                if received_len >= expected_len:
                    print(f"[-] 传输完成: 接收 {received_len}/{expected_len} bytes")
                    current_file.close()
                    current_file = None

if __name__ == '__main__':
    main()
