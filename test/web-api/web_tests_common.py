"""
Shared utilities for blot --web HTTP and WebSocket tests.

Lives in test/web-api/ alongside the test scripts so that
'import web_tests_common' works without any sys.path manipulation.

Usage:
    from web_tests_common import BlotServer, BlotWS

    with BlotServer(ccj_path) as srv:
        data = srv.http_get('/api/status')
        ws   = srv.ws_connect()
        ws.call('initialize', {})
        ws.close()
"""

import base64
import json
import os
import socket as _socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request


# ── Environment ───────────────────────────────────────────────────────────

BLOT_EXE = os.environ.get('BLOT_EXE', './build-Debug/blot')
FIXTURE_DIR = os.environ.get(
    'BLOT_FIXTURE_DIR', os.path.join(os.path.dirname(__file__), '../fixture')
)


# ── Free-port helper ──────────────────────────────────────────────────────


def _free_port():
    """Bind to port 0 to get a free port, then release it."""
    with _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


# ── BlotServer context manager ────────────────────────────────────────────


class BlotServer:
    """
    Context manager that spawns `blot --web` and waits until it is ready.

    with BlotServer(ccj_path) as srv:
        data   = srv.http_get('/api/status')
        result = srv.http_get_raw('/api/source?file=foo.cpp')
        ws     = srv.ws_connect()

    On exit the subprocess is terminated and waited for.
    """

    def __init__(self, ccj_path):
        self.host = '127.0.0.1'
        self.port = _free_port()
        self._ccj_path = os.path.abspath(ccj_path)
        # Run the server from the fixture directory so that relative
        # "directory": "." entries in compile_commands.json resolve correctly.
        self._cwd = os.path.dirname(self._ccj_path)
        self._proc = None

    def __enter__(self):
        self._proc = subprocess.Popen(
            [
                BLOT_EXE,
                '--web',
                '--port',
                str(self.port),
                '--ccj',
                self._ccj_path,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=self._cwd,
        )
        # Poll HTTP until ready (up to 10 seconds)
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            # Check if the process already died (startup error)
            rc = self._proc.poll()
            if rc is not None:
                out, err = b'', b''
                try:
                    out = self._proc.stdout.read()
                    err = self._proc.stderr.read()
                except Exception:
                    pass
                raise RuntimeError(
                    f"blot --web exited early (rc={rc})\n"
                    f"stdout: {out.decode(errors='replace')}\n"
                    f"stderr: {err.decode(errors='replace')}"
                )
            try:
                urllib.request.urlopen(
                    f'http://{self.host}:{self.port}/api/status', timeout=1
                )
                return self
            except urllib.error.URLError:
                time.sleep(0.05)
        # Timed out — dump output and raise
        self._proc.terminate()
        self._proc.wait(timeout=3)
        out = self._proc.stdout.read()
        err = self._proc.stderr.read()
        raise RuntimeError(
            f"blot --web did not become ready on port {self.port}\n"
            f"stdout: {out.decode(errors='replace')}\n"
            f"stderr: {err.decode(errors='replace')}"
        )

    def __exit__(self, *_):
        if self._proc:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait()

    # ── HTTP helpers ──────────────────────────────────────────────────────

    def http_get(self, path):
        """GET path, assert 200, return parsed JSON body."""
        url = f'http://{self.host}:{self.port}{path}'
        with urllib.request.urlopen(url) as resp:
            return json.loads(resp.read())

    def http_get_raw(self, path):
        """GET path, return (status_code, body_bytes).  Does not raise on 4xx."""
        url = f'http://{self.host}:{self.port}{path}'
        try:
            with urllib.request.urlopen(url) as resp:
                return resp.getcode(), resp.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    # ── WebSocket factory ─────────────────────────────────────────────────

    def ws_connect(self):
        """Return a connected BlotWS for JSONRPC calls."""
        return BlotWS(self.host, self.port)


# ── Minimal WebSocket client (no third-party deps) ────────────────────────

_WS_MAGIC = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'


class _RawWS:
    """
    Bare-bones WebSocket client over a raw TCP socket.

    Only implements what the blot tests need:
      - text frames (opcode 0x1)
      - close frames (opcode 0x8)
      - client-side masking (mandatory per RFC 6455)
    """

    def __init__(self, host, port, path='/ws'):
        self._sock = _socket.create_connection((host, port), timeout=15)
        self._handshake(host, port, path)

    def _handshake(self, host, port, path):
        key = base64.b64encode(os.urandom(16)).decode()
        req = (
            f'GET {path} HTTP/1.1\r\n'
            f'Host: {host}:{port}\r\n'
            f'Upgrade: websocket\r\n'
            f'Connection: Upgrade\r\n'
            f'Sec-WebSocket-Key: {key}\r\n'
            f'Sec-WebSocket-Version: 13\r\n'
            f'\r\n'
        )
        self._sock.sendall(req.encode())
        # Read until \r\n\r\n
        buf = b''
        while b'\r\n\r\n' not in buf:
            chunk = self._sock.recv(1)
            if not chunk:
                raise ConnectionError('WS handshake: connection closed')
            buf += chunk
        if b'101' not in buf:
            raise ConnectionError(f'WS handshake failed: {buf!r}')

    def send_text(self, text):
        payload = text.encode('utf-8')
        self._send_frame(0x1, payload)

    def _send_frame(self, opcode, payload):
        n = len(payload)
        mask_key = os.urandom(4)
        masked = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload))
        frame = bytearray()
        frame.append(0x80 | opcode)  # FIN + opcode
        if n < 126:
            frame.append(0x80 | n)  # MASK bit + 7-bit length
        elif n < 65536:
            frame.append(0x80 | 126)
            frame += struct.pack('>H', n)
        else:
            frame.append(0x80 | 127)
            frame += struct.pack('>Q', n)
        frame += mask_key
        frame += masked
        self._sock.sendall(bytes(frame))

    def recv_frame(self):
        """Return (opcode, payload_bytes).  Handles close frames."""
        header = self._recv_exact(2)
        opcode = header[0] & 0x0F
        masked = bool(header[1] & 0x80)
        length = header[1] & 0x7F
        if length == 126:
            length = struct.unpack('>H', self._recv_exact(2))[0]
        elif length == 127:
            length = struct.unpack('>Q', self._recv_exact(8))[0]
        if masked:
            mask_key = self._recv_exact(4)
            data = self._recv_exact(length)
            payload = bytes(b ^ mask_key[i % 4] for i, b in enumerate(data))
        else:
            payload = self._recv_exact(length)
        return opcode, payload

    def _recv_exact(self, n):
        buf = b''
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError('WS recv: connection closed unexpectedly')
            buf += chunk
        return buf

    def recv_text(self):
        """Receive the next text frame, skipping ping frames."""
        while True:
            opcode, payload = self.recv_frame()
            if opcode == 0x1:
                return payload.decode('utf-8')
            if opcode == 0x8:
                raise ConnectionError('WebSocket closed by server')
            if opcode == 0x9:
                # Ping — send pong
                self._send_frame(0xA, payload)

    def close(self):
        try:
            self._send_frame(0x8, b'')
        except Exception:
            pass
        try:
            self._sock.close()
        except Exception:
            pass


# ── JSONRPC-over-WebSocket client ─────────────────────────────────────────


class BlotWS:
    """
    JSONRPC 2.0 over WebSocket client.

    result = ws.call('blot/infer', {'file': 'source.cpp'})
    notifs = ws.pop_notifications()

    call() blocks until the matching response arrives.  Any
    blot/progress notifications received in-flight are buffered
    and returned by pop_notifications().  Raises on JSONRPC error.
    """

    def __init__(self, host, port):
        self._ws = _RawWS(host, port)
        self._next_id = 0
        self._notifications = []

    def call(self, method, params=None):
        """Send a JSONRPC request and wait for the matching response."""
        self._next_id += 1
        req_id = self._next_id
        msg = {'jsonrpc': '2.0', 'id': req_id, 'method': method}
        if params is not None:
            msg['params'] = params
        self._ws.send_text(json.dumps(msg))
        # Drain frames until we see the response for req_id
        while True:
            raw = self._ws.recv_text()
            obj = json.loads(raw)
            if 'method' in obj:
                # Server-sent notification
                self._notifications.append(obj)
            elif obj.get('id') == req_id:
                if 'error' in obj:
                    raise JsonRpcError(obj['error'])
                return obj.get('result', {})

    def pop_notifications(self):
        """Return and clear buffered notifications."""
        n = list(self._notifications)
        self._notifications.clear()
        return n

    def close(self):
        self._ws.close()


class JsonRpcError(Exception):
    """Raised when a JSONRPC response contains an 'error' field."""

    def __init__(self, error_obj):
        self.code = error_obj.get('code')
        self.message = error_obj.get('message', '')
        self.data = error_obj.get('data')
        super().__init__(f"JSONRPC error {self.code}: {self.message}")


# ── Test runner helpers ───────────────────────────────────────────────────


def fixture(name):
    """Return the path to a named fixture directory."""
    return os.path.join(FIXTURE_DIR, name)


def fixture_ccj(name):
    """Return the compile_commands.json path inside a named fixture."""
    return os.path.join(FIXTURE_DIR, name, 'compile_commands.json')


def run_tests(*test_fns):
    """
    Run each test function.  Print PASS/FAIL to stderr (ctest reads stderr).
    Exit 0 on all-pass, 1 on any failure.
    """
    failed = []
    for fn in test_fns:
        try:
            fn()
        except AssertionError as e:
            print(f'FAIL [{fn.__name__}]: {e}', file=sys.stderr)
            failed.append(fn.__name__)
        except Exception as e:
            print(
                f'ERROR [{fn.__name__}]: {type(e).__name__}: {e}',
                file=sys.stderr,
            )
            failed.append(fn.__name__)

    if failed:
        print(f'FAILED: {", ".join(failed)}', file=sys.stderr)
        sys.exit(1)
    else:
        print('PASS', file=sys.stderr)
