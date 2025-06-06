#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import socket
import threading
import time
import signal
import sys
import argparse
import zlib
import random

class TCPTester:
    def __init__(self):
        self.running = True
        self.connected = False
        self.socket = None
        
        # 接收统计
        self.rx_total_bytes = 0
        self.rx_crc32 = 0
        self.rx_packets = 0
        
        # 发送统计
        self.tx_total_bytes = 0
        self.tx_crc32 = 0
        self.tx_packets = 0
        
        # 发送参数
        self.packet_size = 100
        self.send_rate = 0  # 0表示不限速
        self.test_duration = 0  # 0表示不限制
        self.max_packets = 0  # 0表示不限制
        self.enable_send = False
        
        # 时间记录
        self.connect_time = None
        self.disconnect_time = None
        self.send_start_time = None
        self.send_end_time = None
        
    def signal_handler(self, signum, frame):
        """处理CTRL+C信号"""
        print("\n收到中止信号，正在停止测试...")
        self.running = False
    
    def connect_to_server(self, host, port):
        """连接到TCP服务器"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((host, port))
            self.connected = True
            self.connect_time = time.time()
            print(f"成功连接到服务器 {host}:{port}")
            return True
        except Exception as e:
            print(f"连接服务器失败: {e}")
            return False
    
    def receiver_thread(self):
        """接收数据线程"""
        try:
            self.socket.settimeout(1.0)  # 设置超时以便响应中止信号
            
            while self.running and self.connected:
                try:
                    data = self.socket.recv(4096)
                    if not data:
                        print("服务器断开连接")
                        self.connected = False
                        break
                    
                    # 累加统计
                    self.rx_total_bytes += len(data)
                    self.rx_crc32 = zlib.crc32(data, self.rx_crc32)
                    self.rx_packets += 1
                    
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"接收数据时出错: {e}")
                    break
                    
        except Exception as e:
            print(f"接收线程异常: {e}")
        finally:
            if not self.disconnect_time:
                self.disconnect_time = time.time()
            print("接收测试已停止")
    
    def sender_thread(self):
        """发送数据线程"""
        try:
            # 等待连接建立后再等待2秒
            while self.running and not self.connected:
                time.sleep(0.1)
            
            if not self.running:
                return
                
            print("发送测试将在5秒后开始...")
            time.sleep(5)
            
            if not self.running:
                return
            
            print("开始发送测试")
            self.send_start_time = time.time()
            
            packet_interval = 0
            if self.send_rate > 0:
                # 计算包间隔时间 (秒)
                bits_per_packet = self.packet_size * 8
                packet_interval = bits_per_packet / self.send_rate
            
            while self.running and self.connected:
                # 检查测试时长限制
                if self.test_duration > 0:
                    elapsed = time.time() - self.send_start_time
                    if elapsed >= self.test_duration:
                        print(f"达到测试时长限制 ({self.test_duration}秒)")
                        break
                
                # 检查报文数限制
                if self.max_packets > 0 and self.tx_packets >= self.max_packets:
                    print(f"达到报文数限制 ({self.max_packets}个)")
                    break
                
                # 生成随机数据
                data = self.generate_random_data(self.packet_size)
                
                try:
                    send_start = time.time()
                    self.socket.send(data)
                    
                    # 更新统计
                    self.tx_total_bytes += len(data)
                    self.tx_crc32 = zlib.crc32(data, self.tx_crc32)
                    self.tx_packets += 1
                    
                    # 速率控制
                    if packet_interval > 0:
                        send_duration = time.time() - send_start
                        sleep_time = packet_interval - send_duration
                        if sleep_time > 0:
                            time.sleep(sleep_time)
                            
                except Exception as e:
                    print(f"发送数据时出错: {e}")
                    break
            
            self.send_end_time = time.time()
            print("发送测试已停止")
            
        except Exception as e:
            print(f"发送测试失败: {e}")
            self.send_end_time = time.time()
    
    def generate_random_data(self, size):
        """生成指定大小的随机数据"""
        return bytes(random.getrandbits(8) for _ in range(size))
    
    def run_test(self, host, port):
        """运行测试"""
        if not self.connect_to_server(host, port):
            return
        
        try:
            # 启动接收线程
            rx_thread = threading.Thread(target=self.receiver_thread)
            rx_thread.daemon = True
            rx_thread.start()
            
            # 如果启用发送测试，启动发送线程
            if self.enable_send:
                tx_thread = threading.Thread(target=self.sender_thread)
                tx_thread.daemon = True
                tx_thread.start()
            
            # 主循环
            while self.running and self.connected:
                time.sleep(0.1)
                
        except KeyboardInterrupt:
            pass
        finally:
            if not self.disconnect_time:
                self.disconnect_time = time.time()
            self.connected = False
            if self.socket:
                try:
                    self.socket.close()
                except:
                    pass
    
    def print_statistics(self):
        """打印统计信息"""
        print("\n" + "="*50)
        print("测试统计信息")
        print("="*50)
        
        # 计算连接总时长
        total_time = 0
        if self.connect_time and self.disconnect_time:
            total_time = self.disconnect_time - self.connect_time
        
        if self.rx_total_bytes > 0:
            print(f"接收统计:")
            print(f"  总接收字节数: {self.rx_total_bytes:,} bytes")
            print(f"  接收报文数: {self.rx_packets:,}")
            print(f"  接收数据CRC32: 0x{self.rx_crc32 & 0xffffffff:08X}")
            if total_time > 0:
                print(f"  平均接收速率: {(self.rx_total_bytes * 8 / total_time / 1000):.2f} kbps")
        
        if self.tx_total_bytes > 0:
            # 计算发送测试时长
            send_time = 0
            if self.send_start_time and self.send_end_time:
                send_time = self.send_end_time - self.send_start_time
            
            print(f"发送统计:")
            print(f"  总发送字节数: {self.tx_total_bytes:,} bytes")
            print(f"  发送报文数: {self.tx_packets:,}")
            print(f"  发送数据CRC32: 0x{self.tx_crc32 & 0xffffffff:08X}")
            if send_time > 0:
                print(f"  发送测试时长: {send_time:.2f} 秒")
                print(f"  平均发送速率: {(self.tx_total_bytes * 8 / send_time / 1000):.2f} kbps")
        
        if total_time > 0:
            print(f"总连接时长: {total_time:.2f} 秒")

def main():
    parser = argparse.ArgumentParser(description='TCP数据传输测试工具 (客户端)')
    parser.add_argument('host', nargs='?', default='127.0.0.1', help='服务器地址 (默认: 127.0.0.1)')
    parser.add_argument('port', nargs='?', type=int, default=8888, help='服务器端口 (默认: 8888)')
    parser.add_argument('-S', '--send', action='store_true', help='启用发送测试')
    parser.add_argument('-s', '--packet-size', type=int, default=100, help='数据包大小 32-1024字节 (默认: 100)')
    parser.add_argument('-r', '--rate', type=int, default=0, help='发送速率 kbps, 0为不限速 (默认: 0)')
    parser.add_argument('-d', '--duration', type=int, default=0, help='测试时长(秒), 0为不限制 (默认: 0)')
    parser.add_argument('-c', '--count', type=int, default=0, help='发送报文数, 0为不限制 (默认: 0)')
    
    args = parser.parse_args()
    
    # 验证参数
    if args.packet_size < 32 or args.packet_size > 1024:
        print("错误: 数据包大小必须在32-1024字节范围内")
        sys.exit(1)
    
    tester = TCPTester()
    tester.enable_send = args.send
    tester.packet_size = args.packet_size
    tester.send_rate = args.rate * 1000 if args.rate > 0 else 0  # 转换为bps
    tester.test_duration = args.duration
    tester.max_packets = args.count
    
    # 设置信号处理
    signal.signal(signal.SIGINT, tester.signal_handler)
    
    print("TCP数据传输测试工具启动 (客户端模式)")
    print(f"配置参数:")
    print(f"  服务器地址: {args.host}:{args.port}")
    print(f"  数据包大小: {args.packet_size} bytes")
    print(f"  启用发送测试: {'是' if args.send else '否'}")
    if args.send:
        print(f"  发送速率: {'不限速' if args.rate == 0 else f'{args.rate} kbps'}")
        print(f"  测试时长: {'不限制' if args.duration == 0 else f'{args.duration} 秒'}")
        print(f"  报文数限制: {'不限制' if args.count == 0 else f'{args.count} 个'}")
    
    # 运行测试
    tester.run_test(args.host, args.port)
    
    # 等待线程结束
    print("等待测试完成...")
    time.sleep(1)
    
    # 打印统计信息
    tester.print_statistics()

if __name__ == "__main__":
    main()
