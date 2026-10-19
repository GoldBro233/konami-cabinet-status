#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import argparse
import concurrent.futures
import http.client
import json
import selectors
import socket
import subprocess
import time


def wait_line(proc, expected, timeout=5):
    selector = selectors.DefaultSelector()
    selector.register(proc.stdout, selectors.EVENT_READ)
    try:
        assert selector.select(timeout), f'timeout waiting for {expected}'
        line = proc.stdout.readline().strip()
        assert line == expected, (expected, line, proc.poll())
    finally:
        selector.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', required=True)
    args = parser.parse_args()
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    argv = [args.server, '--bind', '127.0.0.1', '--port', str(port)]
    proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, bufsize=1)
    def command(text):
        proc.stdin.write(text + '\n')
        proc.stdin.flush()
        wait_line(proc, 'OK')
    def request(path='/status', method='GET'):
        connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
        try:
            connection.request(method, path)
            reply = connection.getresponse()
            body = reply.read()
            headers = dict((k.lower(), v) for k, v in reply.getheaders())
            assert reply.version == 11
            assert headers['content-type'] == 'application/json'
            assert headers['cache-control'] == 'no-store'
            assert headers['connection'] == 'close'
            assert 'access-control-allow-origin' not in headers
            if method == 'HEAD':
                assert body == b'' and 'content-length' not in headers
            else:
                assert int(headers['content-length']) == len(body)
                json.loads(body)
            return reply.status, headers, body
        finally:
            connection.close()
    def check(code, body, path='/status', method='GET'):
        actual, headers, payload = request(path, method)
        assert (actual, payload) == (code, body.encode()), (actual, payload)
        return headers
    try:
        wait_line(proc, 'READY')
        check(503, '{"error":"state_unavailable"}')
        command('idle')
        check(200, '{"status":"free"}')
        command('login 10000001')
        check(200, '{"status":"occupied"}', '/status?ignored=yes')
        command('ending 10000001')
        check(200, '{"status":"occupied"}')
        command('idle')
        check(200, '{"status":"free"}')
        check(404, '{"error":"not_found"}', '/other')
        check(404, '{"error":"not_found"}', '/foo/../status')
        check(404, '{"error":"not_found"}', '/%73tatus')
        check(404, '{"error":"not_found"}', '/status/')
        headers = check(405, '{"error":"method_not_allowed"}', method='POST')
        assert headers['allow'] == 'GET'
        assert check(405, '', method='HEAD')['allow'] == 'GET'
        check(404, '', path='/other', method='HEAD')
        # Consumers can read concurrently with real state changes.
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            for _ in range(8):
                pending = [pool.submit(request) for _ in range(2)]
                command('login 10000001')
                for job in pending:
                    status, _, payload = job.result()
                    assert status in (200, 503)
                    assert payload in (b'{"status":"free"}', b'{"status":"occupied"}', b'{"error":"state_unavailable"}')
                command('idle')
        conflict = subprocess.run(argv, input='quit\n', capture_output=True, text=True, timeout=5)
        assert conflict.returncode != 0 and 'READY' not in conflict.stdout
        command('get')
        check(503, '{"error":"state_unavailable"}')
        command('idle')
        check(200, '{"status":"free"}')
        command('login 10000001')
        command('protocol-fault')
        check(503, '{"error":"state_unavailable"}')
        command('login 10000002')
        check(503, '{"error":"state_unavailable"}')
        command('idle')
        check(200, '{"status":"free"}')
        command('login 10000002')
        check(200, '{"status":"occupied"}')
        command('fault')
        check(503, '{"error":"state_unavailable"}')
        command('idle')
        check(503, '{"error":"state_unavailable"}')
        # Keep an incomplete request open while stopping the actual server.
        half = socket.create_connection(('127.0.0.1', port), timeout=2)
        half.sendall(b'GET /status HTTP/1.1\r\nHost: localhost\r\n')
        started = time.monotonic()
        command('quit')
        proc.wait(timeout=5)
        elapsed = time.monotonic() - started
        half.close()
        assert proc.returncode == 0
        with socket.socket() as after:
            after.settimeout(1)
            assert after.connect_ex(('127.0.0.1', port)) != 0
        # A non-CivetWeb listener also must prevent READY.
        with socket.socket() as occupied:
            occupied.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            occupied.bind(('127.0.0.1', 0))
            occupied.listen()
            other_port = occupied.getsockname()[1]
            other = subprocess.run([*argv[:-1], str(other_port)], input='quit\n', capture_output=True, text=True, timeout=5)
            assert other.returncode != 0 and 'READY' not in other.stdout
        print(f'HTTP smoke passed: routing, state, concurrency, conflict, stop ({elapsed:.3f}s)')
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.communicate(timeout=5)


if __name__ == '__main__':
    main()
