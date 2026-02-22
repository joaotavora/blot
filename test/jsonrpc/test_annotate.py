#!/usr/bin/env python3
"""Test blot/annotate method"""
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


def test_annotate_simple():
    """Test basic annotation"""
    # Simple assembly input
    assembly = """.file	"test.cpp"
	.text
.Ltext0:
	.file 0 "/tmp" "test.cpp"
	.globl	_Z3addii
	.type	_Z3addii, @function
_Z3addii:
.LFB0:
	.file 1 "test.cpp"
	.loc 1 1 23
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	movl	%edi, -4(%rbp)
	movl	%esi, -8(%rbp)
	.loc 1 2 14
	movl	-4(%rbp), %edx
	movl	-8(%rbp), %eax
	addl	%edx, %eax
	.loc 1 3 1
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE0:
	.size	_Z3addii, .-_Z3addii
"""

    write_message({
        'jsonrpc': '2.0',
        'id': 1,
        'method': 'blot/annotate',
        'params': {
            'assembly': assembly,
            'options': {}
        }
    })

    response = read_message()
    assert response['jsonrpc'] == '2.0'
    assert response['id'] == 1
    assert 'result' in response, f"Expected result, got {response}"

    result = response['result']
    assert 'assembly' in result, f"Expected assembly in result"
    assert 'line_mappings' in result, f"Expected line_mappings in result"
    assert isinstance(result['assembly'], list), f"Expected assembly to be array"
    assert len(result['assembly']) > 0, f"Expected non-empty assembly"


def test_annotate_with_demangle():
    """Test annotation with demangling"""
    assembly = """.file	"test.cpp"
	.text
.Ltext0:
	.file 0 "/tmp" "test.cpp"
	.globl	_Z3addii
	.type	_Z3addii, @function
_Z3addii:
.LFB0:
	.file 1 "test.cpp"
	.loc 1 1 23
	.cfi_startproc
	ret
	.cfi_endproc
.LFE0:
	.size	_Z3addii, .-_Z3addii
"""

    write_message({
        'jsonrpc': '2.0',
        'id': 2,
        'method': 'blot/annotate',
        'params': {
            'assembly': assembly,
            'options': {
                'demangle': True
            }
        }
    })

    response = read_message()
    assert response['jsonrpc'] == '2.0'
    assert response['id'] == 2
    assert 'result' in response


if __name__ == '__main__':
    try:
        test_annotate_simple()
        test_annotate_with_demangle()

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
