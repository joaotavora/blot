#!/usr/bin/env python3
"""
Test session-level caching behaviour:
  - grab_asm returns cached="token" on second call with the asm token
  - annotate returns cached="token" on second call with the same asm token
  - infer returns cached="token" when called with a previous token
  - cache is scoped to the WS session (new connection starts fresh)

cached="other" scenarios live in test_ws_cache_other_pipelines.py and
test_ws_cache_other_inference.py.
"""

from web_tests_common import BlotServer, fixture_ccj, run_tests


CCJ = fixture_ccj('gcc-minimal')


def _run_pipeline(ws):
    """Run the full pipeline once; return (infer_token, asm_token, annotate_token).

    Since tokens are minted early, all three values are the same integer.
    """
    ws.call('initialize', {})
    infer_res = ws.call('blot/infer', {'file': 'source.cpp'})
    asm_res = ws.call('blot/grab_asm', {'token': infer_res['token']})
    ann_res = ws.call(
        'blot/annotate',
        {
            'token': asm_res['token'],
            'options': {'demangle': False},
        },
    )
    return infer_res['token'], asm_res['token'], ann_res['token']


def test_grabasm_cache_token():
    """grab_asm called with the asm token it returned hits asm_cache_1 (cached='token')."""
    with BlotServer(CCJ) as srv:
        ws = srv.ws_connect()
        try:
            _, asm_tok, _ = _run_pipeline(ws)

            asm_res2 = ws.call('blot/grab_asm', {'token': asm_tok})
            assert asm_res2['cached'] == 'token', (
                f'expected cached="token", got {asm_res2["cached"]!r}'
            )
        finally:
            ws.close()


def test_annotate_cache_token():
    """Annotate called with its own returned token hits annotate_cache_1 (cached='token')."""
    with BlotServer(CCJ) as srv:
        ws = srv.ws_connect()
        try:
            _, _, ann_tok = _run_pipeline(ws)

            # Re-annotate using the token the first annotate call returned.
            ann_res2 = ws.call(
                'blot/annotate',
                {
                    'token': ann_tok,
                    'options': {'demangle': False},
                },
            )
            assert ann_res2['cached'] == 'token', (
                f'expected cached="token" on re-annotate, got {ann_res2["cached"]!r}'
            )
        finally:
            ws.close()


def test_infer_cache_token():
    """blot/infer called with a previous token returns cached='token'."""
    with BlotServer(CCJ) as srv:
        ws = srv.ws_connect()
        try:
            infer_tok, _, _ = _run_pipeline(ws)

            infer_res2 = ws.call('blot/infer', {'token': infer_tok})
            assert infer_res2['cached'] == 'token', (
                f'expected cached="token" on re-infer, got {infer_res2["cached"]!r}'
            )
            assert infer_res2['token'] == infer_tok, (
                f'token changed on cache hit: {infer_res2["token"]} != {infer_tok}'
            )
        finally:
            ws.close()


def test_cache_is_session_scoped():
    """A new WS connection starts with empty caches; stale tokens are rejected."""
    from web_tests_common import JsonRpcError

    with BlotServer(CCJ) as srv:
        ws1 = srv.ws_connect()
        try:
            infer_tok, asm_tok, _ = _run_pipeline(ws1)
        finally:
            ws1.close()

        # New session — tokens from ws1 must not be visible
        ws2 = srv.ws_connect()
        try:
            ws2.call('initialize', {})
            try:
                ws2.call('blot/infer', {'token': infer_tok})
                assert False, 'expected token to be unknown in new session'
            except JsonRpcError as e:
                assert e.code == -32602, f'unexpected error code: {e.code}'
        finally:
            ws2.close()


if __name__ == '__main__':
    run_tests(
        test_grabasm_cache_token,
        test_annotate_cache_token,
        test_infer_cache_token,
        test_cache_is_session_scoped,
    )
