#!/usr/bin/env python3
"""Test JSONRPC error handling"""
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


def test_method_not_found():
    """Test unknown method error"""
    write_message({
        'jsonrpc': '2.0',
        'id': 1,
        'method': 'unknown/method',
        'params': {}
    })

    response = read_message()
    assert response['jsonrpc'] == '2.0'
    assert response['id'] == 1
    assert 'error' in response, f"Expected error in response"
    assert response['error']['code'] == -32601, f"Expected METHOD_NOT_FOUND (-32601)"


def test_invalid_params():
    """Test invalid parameters error"""
    write_message({
        'jsonrpc': '2.0',
        'id': 2,
        'method': 'blot/annotate',
        'params': {
            # Missing 'assembly' parameter
            'options': {}
        }
    })

    response = read_message()
    assert response['jsonrpc'] == '2.0'
    assert response['id'] == 2
    assert 'error' in response
    assert response['error']['code'] == -32602, f"Expected INVALID_PARAMS (-32602)"


def test_parse_error():
    """Test malformed JSON"""
    # Write invalid JSON directly
    invalid_content = "{invalid}"
    invalid_json = f"Content-Length: {len(invalid_content)}\r\n\r\n{invalid_content}"
    server.stdin.write(invalid_json.encode('utf-8'))
    server.stdin.flush()

    response = read_message()
    assert response['jsonrpc'] == '2.0'
    assert 'error' in response
    assert response['error']['code'] == -32700, f"Expected PARSE_ERROR (-32700)"


if __name__ == '__main__':
    try:
        test_method_not_found()
        test_invalid_params()
        test_parse_error()

        # Send exit notification
        write_message({'jsonrpc': '2.0', 'method': 'exit'})

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
