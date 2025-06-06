#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import socket
import threading
import signal
import sys
import argparse
import time

class TCPEchoServer:
    def __init__(self, host='0.0.0.0', port=8888):
        self.host = host
        self.port = port
        self.running = True
        self.server_socket = None
        self.client_count = 0
        self.total_bytes = 0
        self.start_time = None
        self.stop_time = None
        
    def signal_handler(self, signum, frame):
        """处理CTRL+C信号"""
        print("\n收到中止信号，正在停止服务器...")
        self.running = False
        if not self.stop_time:
            self.stop_time = time.time()
        if self.server_socket:
            self.server_socket.close()
    
    def handle_client(self, client_socket, addr):
        """处理客户端连接"""
        print(f"Client {addr} connected")
        client_bytes = 0
        
        try:
            while self.running:
                data = client_socket.recv(4096)
                if not data:
                    break
                
                # Echo the data back
                client_socket.send(data)
                
                # Update statistics
                client_bytes += len(data)
                self.total_bytes += len(data)
                
        except Exception as e:
            print(f"Error handling client {addr}: {e}")
        finally:
            client_socket.close()
            print(f"Client {addr} disconnected (processed {client_bytes:,} bytes)")
    
    def start_server(self):
        """启动TCP echo服务器"""
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(10)
            
            print(f"TCP Echo Server started on {self.host}:{self.port}")
            print("Waiting for connections...")
            self.start_time = time.time()
            
            while self.running:
                try:
                    client_socket, addr = self.server_socket.accept()
                    self.client_count += 1
                    
                    # 为每个客户端创建线程
                    client_thread = threading.Thread(
                        target=self.handle_client,
                        args=(client_socket, addr)
                    )
                    client_thread.daemon = True
                    client_thread.start()
                    
                except socket.error:
                    if self.running:
                        print("Socket error occurred")
                    break
                except Exception as e:
                    if self.running:
                        print(f"Error accepting connection: {e}")
                    break
            
        except Exception as e:
            print(f"Failed to start server: {e}")
        finally:
            if not self.stop_time:
                self.stop_time = time.time()
            if self.server_socket:
                self.server_socket.close()
            self.print_statistics()
    
    def print_statistics(self):
        """打印服务器统计信息"""
        elapsed = 0
        if self.start_time and self.stop_time:
            elapsed = self.stop_time - self.start_time
            
        print("\n" + "="*50)
        print("TCP Echo Server Statistics")
        print("="*50)
        print(f"Running time: {elapsed:.2f} seconds")
        print(f"Total clients served: {self.client_count}")
        print(f"Total bytes processed: {self.total_bytes:,} bytes")
        if elapsed > 0:
            print(f"Average throughput: {(self.total_bytes * 8 / elapsed / 1000):.2f} kbps")

def main():
    parser = argparse.ArgumentParser(description='TCP Echo Server')
    parser.add_argument('--host', type=str, default='0.0.0.0', 
                       help='Server host address (default: 0.0.0.0)')
    parser.add_argument('--port', type=int, default=8888, 
                       help='Server port (default: 8888)')
    
    args = parser.parse_args()
    
    # 创建echo服务器
    server = TCPEchoServer(args.host, args.port)
    
    # 设置信号处理
    signal.signal(signal.SIGINT, server.signal_handler)
    
    try:
        server.start_server()
    except KeyboardInterrupt:
        pass
    
    print("TCP Echo Server stopped")

if __name__ == "__main__":
    main()
