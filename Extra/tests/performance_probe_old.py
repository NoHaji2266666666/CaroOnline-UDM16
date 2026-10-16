#!/usr/bin/env python3
"""2-load performance probe. Run twice with --clients 5 and --clients 20.
Measures latency for broadcasting room-list updates to connected clients.
Does not validate Windows resource usage; record CPU/RAM separately.
"""
import argparse
import json
import socket
import statistics
import time
import uuid


def send(sock, obj):
    sock.sendall((json.dumps(obj) + '\n').encode())


def until(sock, buffers, idx, expected):
    while True:
        if b'\n' in buffers[idx]:
            line, buffers[idx] = buffers[idx].split(b'\n', 1)
            msg = json.loads(line)
            if msg.get('type') == expected:
                return msg
            continue
        part = sock.recv(65536)
        if not part:
            raise ConnectionError('server closed')
        buffers[idx] += part


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=45454)
    ap.add_argument('--clients', type=int, default=5)
    ap.add_argument('--requests', type=int, default=20)
    args = ap.parse_args()
    assert 1 <= args.clients <= 50 and 1 <= args.requests <= 500
    sockets = []
    buffers = [b'' for _ in range(args.clients)]
    prefix = 'p_' + uuid.uuid4().hex[:8]
    try:
        for i in range(args.clients):
            sock = socket.create_connection((args.host, args.port), timeout=10)
            sock.settimeout(10)
            sockets.append(sock)
            send(sock, {'type': 'hello', 'name': f'{prefix}_{i}', 'token': ''})
            until(sock, buffers, i, 'welcome')
        latency = []
        for _ in range(args.requests):
            t0 = time.perf_counter()
            send(sockets[0], {'type': 'list_rooms'})
            until(sockets[0], buffers, 0, 'rooms')
            latency.append((time.perf_counter() - t0)*1000)
        latency.sort()
        print(f'connected_clients={args.clients} requests={args.requests} '
              f'mean_ms={statistics.mean(latency):.2f} '
              f'p95_ms={latency[max(0, int(len(latency)*.95)-1)]:.2f} '
              f'max_ms={max(latency):.2f}')
    finally:
        for sock in sockets: sock.close()


if __name__ == '__main__':
    main()
