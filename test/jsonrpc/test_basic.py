#!/usr/bin/env python3
"""Test basic JSONRPC lifecycle (initialize, shutdown, exit)"""
import sys
import os
import subprocess
import json
import re

# Get server path from environment or use default
SERVER_PATH = os.environ.get('BLOT_JSONRPC_SERVER', './build-Debug/blot_jsonrpc_server')

# Start server as subprocess
server = subprocess.Popen(
    [SERVER_PATH],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE
)


def write_message(msg):
    """Write a JSONRPC message with Content-Length framing"""
    json_str = json.dumps(msg)
    content = f"Content-Length: {len(json_str)}\r\n\r\n{json_str}"
    server.stdin.write(content.encode('utf-8'))
    server.stdin.flush()


def read_message():
    """Read a JSONRPC message with Content-Length framing"""
    # Read headers until \r\n\r\n
    headers = b''
    while b'\r\n\r\n' not in headers:
        chunk = server.stdout.read(1)
        if not chunk:
            return None
        headers += chunk

    # Parse Content-Length
    header_str = headers.decode('utf-8')
    match = re.search(r'Content-Length:\s*(\d+)', header_str)
    if not match:
        raise ValueError("Missing Content-Length header")

    content_length = int(match.group(1))

    # Read exact content bytes
    content = server.stdout.read(content_length)
    return json.loads(content.decode('utf-8'))


def test_initialize():
    """Test initialize method"""
    write_message({
        'jsonrpc': '2.0',
        'id': 1,
        'method': 'initialize',
        'params': {'capabilities': {}}
    })

    response = read_message()
    assert response['jsonrpc'] == '2.0', f"Expected jsonrpc 2.0, got {response.get('jsonrpc')}"
    assert response['id'] == 1, f"Expected id 1, got {response.get('id')}"
    assert 'result' in response, f"Expected result in response, got {response}"
    assert 'serverInfo' in response['result'], f"Expected serverInfo in result"
    assert response['result']['serverInfo']['name'] == 'blot-jsonrpc'


def test_shutdown():
    """Test shutdown method"""
    write_message({
        'jsonrpc': '2.0',
        'id': 2,
        'method': 'shutdown',
        'params': {}
    })

    response = read_message()
    assert response['jsonrpc'] == '2.0'
    assert response['id'] == 2
    assert response['result'] is None


def test_exit():
    """Test exit notification"""
    write_message({
        'jsonrpc': '2.0',
        'method': 'exit'
    })
    # No response expected for notifications


if __name__ == '__main__':
    try:
        test_initialize()
        test_shutdown()
        test_exit()

        # Wait for server to exit
        server.wait(timeout=2)

        print("PASS", file=sys.stderr)
    except AssertionError as e:
        server.kill()
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        server.kill()
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
