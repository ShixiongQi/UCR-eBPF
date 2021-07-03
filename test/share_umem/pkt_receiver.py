#!/bin/python3
import argparse
import socket
import sys
import time

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="generate ipv6 udp packets")
    parser.add_argument("--ip", default="fc00:dead:cafe:1::1", help="the destination ipv6 address")
    parser.add_argument("--port", default="1234", help="the destination port of udp packet")
    args = parser.parse_args()
    args.port = int(args.port)

    s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM, 0)
    s.bind((args.ip, args.port))
    print("listening on (%s, %s)" % (args.ip, args.port))
    
    while True:
        data, addr = s.recvfrom(4096)
        print("client %s send %s" % (addr, data))
