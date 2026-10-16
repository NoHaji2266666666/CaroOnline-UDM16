#!/usr/bin/env python3
"""UDM_16 functional TCP smoke tests (Python 3 stdlib only).
Start CaroServer on dedicated test instance before running.
Exit 0 => basic flows passed on the server actually tested.
"""
import argparse
import json
import socket
import time
import uuid


class Peer:
    def __init__(self, host, port, name):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(6)
        self.name = name
        self.incoming = b''
        self.send('hello', name=name, token='')
        welcome = self.wait('welcome')
        self.token = welcome['token']

    def send(self, kind, **kwargs):
        self.sock.sendall((json.dumps(dict(type=kind, **kwargs), ensure_ascii=False) + '\n').encode())

    def wait(self, expected, timeout=6, predicate=None):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            if b'\n' in self.incoming:
                line, self.incoming = self.incoming.split(b'\n', 1)
                msg = json.loads(line)
                if msg.get('type') == expected and (predicate is None or predicate(msg)):
                    return msg
                continue
            self.sock.settimeout(max(.1, until - time.monotonic()))
            self.incoming += self.sock.recv(65536)
        raise AssertionError(f'{self.name}: missing {expected}')

    def close(self):
        self.sock.close()


def main(host, port):
    prefix = 't_' + uuid.uuid4().hex[:7]
    alice, bob, watcher = [Peer(host, port, prefix + suffix) for suffix in ('a', 'b', 'c')]
    try:
        alice.send('invite', target=bob.name)
        assert bob.wait('invite_received')['from'] == alice.name
        bob.send('invite_reply', **{'from': alice.name, 'accept': True})
        start_a = alice.wait('snapshot')
        start_b = bob.wait('snapshot')
        assert start_a['room'] == start_b['room']
        room = start_a['room']
        assert start_a['turn'] == 'X'
        watcher.send('spectate', room=room)
        observed = watcher.wait('snapshot')
        assert observed['board'] == start_a['board']
        watcher.send('move', room=room, row=0, col=0)
        assert 'quyền' in watcher.wait('error')['message']
        bob.send('move', room=room, row=0, col=0)
        assert 'lượt' in bob.wait('error')['message']
        # X wins with 5 horizontal tokens, O places far away.
        for i in range(5):
            alice.send('move', room=room, row=7, col=i)
            state = alice.wait('snapshot', predicate=lambda m, i=i: m.get('lastCol') == i and m['board'][7][i] == 'X')
            if i < 4:
                bob.send('move', room=room, row=0, col=i)
                bob.wait('snapshot', predicate=lambda m, i=i: m.get('lastCol') == i and m['board'][0][i] == 'O')
        assert state['active'] is False and state['winner'] == alice.name
        watcher.send('history')
        history = watcher.wait('history_result', predicate=lambda m: any(item['room'] == room for item in m['matches']))
        assert any(item['winner'] == alice.name for item in history['matches'])
        print('PASS: login, invite, matching, spectator snapshot, deny invalid move, turns, victory, SQLite history')
        # Reconnect is tested with a new match because previous one ended.
        alice.send('invite', target=bob.name)
        bob.wait('invite_received')
        bob.send('invite_reply', **{'from': alice.name, 'accept': True})
        second = alice.wait('snapshot', predicate=lambda m: m['room'] != room)
        new_room = second['room']
        alice.close()
        time.sleep(.4)
        restored = socket.create_connection((host, port), timeout=5)
        restored.settimeout(5)
        restored.sendall((json.dumps({'type': 'hello', 'name': alice.name, 'token': alice.token})+'\n').encode())
        data = b''
        until = time.monotonic() + 5
        while b'\n' not in data and time.monotonic() < until:
            data += restored.recv(65536)
        welcome = json.loads(data.split(b'\n')[0])
        assert welcome['type'] == 'welcome' and welcome['resumed']
        print('PASS: TCP reconnect with session token')
        restored.close()
        print('SMOKE TEST PASSED')
    finally:
        bob.close()
        watcher.close()
        try: alice.close()
        except OSError: pass


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=45454)
    args = ap.parse_args()
    main(args.host, args.port)
