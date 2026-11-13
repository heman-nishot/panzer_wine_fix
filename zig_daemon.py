#!/usr/bin/env python3
"""
zig_daemon.py — Multi-threaded Linux compilation daemon for Wine builds.

Receives compilation commands from zig_proxy.c over TCP (127.0.0.1:45678),
converts Windows paths (including paths inside response files like @link.rsp)
to Linux host paths, invokes the native Linux Zig compiler, and streams
stdout, stderr, and the return code back to Wine.
"""

import os
import sys
import socket
import struct
import json
import subprocess
import re
import threading

PORT = 45678
WINE_PREFIX = os.path.expanduser(os.environ.get("WINEPREFIX", "~/.wine-panzer"))
NATIVE_ZIG = os.environ.get("NATIVE_ZIG", os.path.join(WINE_PREFIX, "zig-native/zig"))
WINE_DRIVE_C = os.path.join(WINE_PREFIX, "drive_c")

def win_to_unix(text):
    if not text:
        return text
    
    def replace_drive(match):
        drive = match.group(1).lower()
        subpath = match.group(2).replace('\\', '/').lstrip('/')
        if drive == 'c':
            return os.path.join(WINE_DRIVE_C, subpath)
        elif drive == 'z':
            return '/' + subpath
        else:
            dos_dev = os.path.join(WINE_PREFIX, f"dosdevices/{drive}:")
            return os.path.join(dos_dev, subpath)

    # 1. Quoted paths: "C:\path with spaces\file.ext" or "C:/..."
    text = re.sub(r'\"([A-Za-z]):[\\/]([^\"<>|]+)\"', lambda m: f'"{replace_drive(m)}"', text)
    # 2. Single-quoted paths: 'C:\path with spaces\file.ext'
    text = re.sub(r'\'([A-Za-z]):[\\/]([^\'<>|]+)\'', lambda m: f"'{replace_drive(m)}'", text)
    # 3. Unquoted paths: C:\path or C:/path
    text = re.sub(r'([A-Za-z]):[\\/]([^\s"\'<>|]+)', replace_drive, text)
    return text

def process_arg(arg):
    converted = win_to_unix(arg)
    
    # Handle linker response files (@file.rsp)
    if converted.startswith('@'):
        rsp_path = converted[1:].strip('"').strip("'")
        if os.path.isfile(rsp_path):
            try:
                with open(rsp_path, 'r', encoding='utf-8', errors='replace') as f:
                    content = f.read()
                
                # Convert Windows paths inside the response file
                conv_content = win_to_unix(content)
                # Normalize any remaining backslashes in paths
                conv_content = conv_content.replace('\\', '/')
                
                new_rsp = rsp_path + ".linux.rsp"
                with open(new_rsp, 'w', encoding='utf-8') as f:
                    f.write(conv_content)
                
                return '@' + new_rsp
            except Exception as e:
                print(f"[zig-daemon] Error converting response file {rsp_path}: {e}", flush=True)
                
    return converted

def handle_client(conn, client_addr):
    try:
        raw_len = conn.recv(4)
        if len(raw_len) < 4:
            return
        payload_len = struct.unpack(">I", raw_len)[0]
        
        chunks = []
        bytes_read = 0
        while bytes_read < payload_len:
            chunk = conn.recv(min(4096, payload_len - bytes_read))
            if not chunk:
                break
            chunks.append(chunk)
            bytes_read += len(chunk)
        
        payload = json.loads(b''.join(chunks).decode('utf-8'))
        raw_cwd = payload.get("cwd", "")
        raw_args = payload.get("args", [])

        conv_cwd = win_to_unix(raw_cwd) if raw_cwd else os.getcwd()
        if not os.path.isdir(conv_cwd):
            conv_cwd = WINE_DRIVE_C

        conv_args = [process_arg(a) for a in raw_args]
        cmd = [NATIVE_ZIG] + conv_args

        proc = subprocess.Popen(
            cmd,
            cwd=conv_cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0
        )

        stdout_data, stderr_data = proc.communicate()
        exit_code = proc.returncode

        if stdout_data:
            conn.sendall(b'\x01' + struct.pack(">I", len(stdout_data)) + stdout_data)
        
        if stderr_data:
            conn.sendall(b'\x02' + struct.pack(">I", len(stderr_data)) + stderr_data)

        conn.sendall(b'\x03' + struct.pack(">i", exit_code))

    except Exception as e:
        err_msg = f"[zig-daemon error] {e}\n".encode('utf-8')
        try:
            conn.sendall(b'\x02' + struct.pack(">I", len(err_msg)) + err_msg)
            conn.sendall(b'\x03' + struct.pack(">i", 1))
        except:
            pass
    finally:
        try:
            conn.close()
        except:
            pass

def main():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', PORT))
    server.listen(128)
    print(f"[zig-daemon] Multi-threaded listening on 127.0.0.1:{PORT}", flush=True)

    while True:
        try:
            conn, client_addr = server.accept()
            t = threading.Thread(target=handle_client, args=(conn, client_addr), daemon=True)
            t.start()
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"[zig-daemon] Error accepting: {e}", flush=True)

    server.close()

if __name__ == "__main__":
    main()
