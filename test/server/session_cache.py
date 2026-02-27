#!/usr/bin/env python3
"""
Test session-level caching behaviour:
  - grab_asm returns cached="token" on second call with the asm token
  - annotate returns cached="token" on second call with the same asm token
  - infer returns cached="token" when called with a previous token
  - cache is scoped to the session (new connection starts fresh)

cached="other" scenarios live in session_cache_other_pipelines.py and
session_cache_other_inference.py.
"""

from common import BlotServer, fixture_ccj, run_tests


CCJ = fixture_ccj('gcc-minimal')


def _run_pipeline(endpoint):
    """Run the full pipeline once; return (infer_token, asm_token, annotate_token).

    Since tokens are minted early, all three values are the same integer.
    """
    endpoint.call('initialize', {})
    infer_res = endpoint.call('blot/infer', {'file': 'source.cpp'})
    asm_res = endpoint.call('blot/grab_asm', {'token': infer_res['token']})
    ann_res = endpoint.call(
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
        endpoint = srv.connect()
        try:
            _, asm_tok, _ = _run_pipeline(endpoint)

            asm_res2 = endpoint.call('blot/grab_asm', {'token': asm_tok})
            assert asm_res2['cached'] == 'token', (
                f'expected cached="token", got {asm_res2["cached"]!r}'
            )
        finally:
            endpoint.close()


def test_annotate_cache_token():
    """Annotate called with its own returned token hits annotate_cache_1 (cached='token')."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            _, _, ann_tok = _run_pipeline(endpoint)

            # Re-annotate using the token the first annotate call returned.
            ann_res2 = endpoint.call(
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
            endpoint.close()


def test_infer_cache_token():
    """blot/infer called with a previous token returns cached='token'."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            infer_tok, _, _ = _run_pipeline(endpoint)

            infer_res2 = endpoint.call('blot/infer', {'token': infer_tok})
            assert infer_res2['cached'] == 'token', (
                f'expected cached="token" on re-infer, got {infer_res2["cached"]!r}'
            )
            assert infer_res2['token'] == infer_tok, (
                f'token changed on cache hit: {infer_res2["token"]} != {infer_tok}'
            )
        finally:
            endpoint.close()


def test_cache_is_session_scoped():
    """A new connection starts with empty caches; stale tokens are rejected."""
    from common import JsonRpcError

    with BlotServer(CCJ) as srv:
        ep1 = srv.connect()
        try:
            infer_tok, asm_tok, _ = _run_pipeline(ep1)
        finally:
            ep1.close()

        # New session — tokens from ep1 must not be visible
        ep2 = srv.connect()
        try:
            ep2.call('initialize', {})
            try:
                ep2.call('blot/infer', {'token': infer_tok})
                assert False, 'expected token to be unknown in new session'
            except JsonRpcError as e:
                assert e.code == -32602, f'unexpected error code: {e.code}'
        finally:
            ep2.close()


if __name__ == '__main__':
    run_tests(
        test_grabasm_cache_token,
        test_annotate_cache_token,
        test_infer_cache_token,
        test_cache_is_session_scoped,
    )
