#!/usr/bin/env python3
import serial
import sys
import os
import time
import struct

def upload_kernel(port, baud, filename):
    if not os.path.exists(filename):
        print(f"Error: {filename} not found.")
        return

    file_size = os.path.getsize(filename)
    print(f"Uploading {filename} ({file_size} bytes) to {port} at {baud} baud...")

    try:
        with serial.Serial(port, baud, timeout=2) as ser:
            # Send length (4 bytes, little endian)
            ser.write(struct.pack("<I", file_size))
            
            with open(filename, "rb") as f:
                data = f.read()
                
                start_time = time.time()
                bytes_sent = 0
                chunk_size = 1024
                
                for i in range(0, len(data), chunk_size):
                    chunk = data[i:i+chunk_size]
                    ser.write(chunk)
                    bytes_sent += len(chunk)
                    
                    elapsed = time.time() - start_time
                    speed = (bytes_sent / 1024) / elapsed if elapsed > 0 else 0
                    progress = (bytes_sent / file_size) * 100
                    
                    print(f"\rProgress: {progress:6.2f}% | {bytes_sent/1024:7.1f} KB | {speed:6.1f} KB/s", end="")
                    
                print(f"\nUpload complete in {time.time() - start_time:.2f} seconds.")
                
    except Exception as e:
        print(f"\nError: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 serial_upload.py <port> <kernel_bin>")
        print("Example: python3 serial_upload.py /dev/ttyUSB0 kernel_payload.bin")
        sys.exit(1)

    port = sys.argv[1]
    filename = sys.argv[2]
    upload_kernel(port, 115200, filename)
