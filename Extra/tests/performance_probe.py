#!/usr/bin/env python3
r"""CaroOnline UDM_16: performance probe with visible progress and bounded waits.

Measures localhost RTT of list_rooms requests. This is NOT a full game stress test.
Requires Python 3.8+ only (standard library).

Usage:
  py Extra\tests\performance_probe.py --clients 5 --requests 500
  py Extra\tests\performance_probe.py --clients 20 --requests 500
  py Extra\tests\performance_probe.py --clients 50 --requests 100
"""

import argparse
import json
import math
import select
import socket
import statistics
import sys
import time
import uuid


def transmit(sock, message):
    sock.sendall((json.dumps(message, separators=(',', ':')) + '\n').encode('utf-8'))


def poll(sockets, pending, timeout):
    """Read pending TCP frames from every Client, to avoid unread broadcast buildup."""
    readable, _, _ = select.select(sockets, [], [], max(0.0, timeout))
    received = []
    for sock in readable:
        chunk = sock.recv(65536)
        if not chunk:
            raise ConnectionError('The Server closed a Client connection.')
        pending[sock] += chunk
        if len(pending[sock]) > 1024 * 1024:
            raise ValueError('More than 1 MB of incomplete TCP data received.')
        while b'\n' in pending[sock]:
            frame, pending[sock] = pending[sock].split(b'\n', 1)
            if not frame:
                continue
            try:
                received.append((sock, json.loads(frame)))
            except json.JSONDecodeError as error:
                raise ValueError('Server sent invalid JSON: ' + str(error)) from error
    return received


def wait_type(sock, desired, sockets, pending, timeout, stage):
    deadline = time.monotonic() + timeout
    while True:
        left = deadline - time.monotonic()
        if left <= 0:
            raise TimeoutError(f'{stage}: no {desired!r} response within {timeout:g} seconds')
        for from_socket, message in poll(sockets, pending, min(left, 0.5)):
            if from_socket is not sock:
                continue
            if message.get('type') == 'error':
                raise RuntimeError(f'{stage}: Server error: {message.get("message")}')
            if message.get('type') == desired:
                return message


def drain_notifications(sockets, pending):
    """Discard unsolicited login/broadcast messages before latency samples."""
    deadline = time.monotonic() + 4.0
    while time.monotonic() < deadline:
        # 0.1 s of no new notifications indicates the initial broadcasts have ended.
        if not poll(sockets, pending, 0.1):
            break


def main():
    parser = argparse.ArgumentParser(description='CaroOnline list_rooms latency test')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=45454)
    parser.add_argument('--clients', type=int, default=5)
    parser.add_argument('--requests', type=int, default=20)
    parser.add_argument('--timeout', type=float, default=8.0,
                        help='seconds per login or request (default: 8)')
    args = parser.parse_args()
    if not (1 <= args.clients <= 100 and 1 <= args.requests <= 5000 and args.timeout > 0):
        parser.error('clients=1..100, requests=1..5000, timeout > 0 are required')

    print(f'Starting: {args.clients} Client(s), {args.requests} request(s), '
          f'{args.host}:{args.port}', flush=True)
    sockets = []
    pending = {}
    prefix = 'p_' + uuid.uuid4().hex[:8]
    try:
        for i in range(args.clients):
            sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
            sock.settimeout(args.timeout)
            sockets.append(sock)
            pending[sock] = b''
            transmit(sock, {'type': 'hello', 'name': f'{prefix}_{i}', 'token': ''})
            wait_type(sock, 'welcome', sockets, pending, args.timeout,
                      f'Client {i + 1}/{args.clients} login')
            if (i + 1) % 5 == 0 or i + 1 == args.clients:
                print(f'Connected: {i + 1}/{args.clients}', flush=True)

        drain_notifications(sockets, pending)
        print('All Client(s) connected. Measuring requests...', flush=True)
        timings = []
        interval = max(1, args.requests // 10)
        primary = sockets[0]
        for i in range(args.requests):
            # Prevent stale broadcasts from being counted as new replies.
            while poll(sockets, pending, 0.0):
                pass
            start = time.perf_counter()
            transmit(primary, {'type': 'list_rooms'})
            wait_type(primary, 'rooms', sockets, pending, args.timeout,
                      f'Request {i + 1}/{args.requests}')
            timings.append((time.perf_counter() - start) * 1000.0)
            if (i + 1) % interval == 0 or i + 1 == args.requests:
                print(f'Requests: {i + 1}/{args.requests}', flush=True)

        ordered = sorted(timings)
        p95 = ordered[max(0, math.ceil(len(ordered) * 0.95) - 1)]
        print(f'OK connected_clients={args.clients} requests={args.requests} '
              f'mean_ms={statistics.mean(timings):.2f} '
              f'p95_ms={p95:.2f} max_ms={max(timings):.2f}', flush=True)
        print('NOTE: This tests room-list request latency on the configured network '
              'only, not full-game stress or CPU/RAM.', flush=True)
    except (OSError, TimeoutError, ValueError, RuntimeError, ConnectionError) as exc:
        print(f'TEST FAILED: {exc}', file=sys.stderr, flush=True)
        return 1
    except KeyboardInterrupt:
        print('\nStopped using Ctrl+C.', flush=True)
        return 130
    finally:
        for sock in sockets:
            try:
                sock.close()
            except OSError:
                pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
