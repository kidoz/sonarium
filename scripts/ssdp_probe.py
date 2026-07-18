#!/usr/bin/env python3
"""Manual SSDP diagnostic: multicast one M-SEARCH and print every responder.

Not an automated test — it needs a live LAN and a running MediaServer.
Use it to confirm sonarium-dlna answers discovery from another machine:

    python3 scripts/ssdp_probe.py
"""

import socket

MSEARCH = (
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    'MAN: "ssdp:discover"\r\n'
    "MX: 2\r\n"
    "ST: ssdp:all\r\n"
    "\r\n"
)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.settimeout(2)
sock.sendto(MSEARCH.encode("utf-8"), ("239.255.255.250", 1900))

try:
    while True:
        data, addr = sock.recvfrom(65507)
        print(addr, data.decode("utf-8", errors="ignore"))
except socket.timeout:
    pass
