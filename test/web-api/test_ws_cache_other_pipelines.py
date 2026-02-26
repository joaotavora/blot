#!/usr/bin/env python3
"""
Two independent pipelines on the same file: second grab_asm hits asm_cache_2.

Pipeline A does infer+grab_asm, populating asm_cache_2 under tok_a.
Pipeline B does a fresh infer (tok_b != tok_a), then grab_asm(tok_b): it misses
asm_cache_1, falls through infer_cache_1 to recover the same command, then hits
asm_cache_2 → cached='other', returns tok_a.
"""

from web_tests_common import BlotServer, fixture_ccj, run_tests


CCJ = fixture_ccj('gcc-minimal')


def test_grabasm_cache_other_two_pipelines():
    with BlotServer(CCJ) as srv:
        ws = srv.ws_connect()
        try:
            ws.call('initialize', {})

            # Pipeline A: populate asm_cache_2
            infer_a = ws.call('blot/infer', {'file': 'source.cpp'})
            tok_a = infer_a['token']
            asm_a = ws.call('blot/grab_asm', {'token': tok_a})
            assert asm_a['cached'] is False, 'pipeline A grab_asm should be uncached'

            # Pipeline B: fresh infer yields a new token
            infer_b = ws.call('blot/infer', {'file': 'source.cpp'})
            tok_b = infer_b['token']
            assert tok_b != tok_a, 'second infer should produce a distinct token'

            # grab_asm(tok_b): misses asm_cache_1, falls through, hits asm_cache_2
            asm_b = ws.call('blot/grab_asm', {'token': tok_b})
            assert asm_b['cached'] == 'other', (
                f'expected cached="other", got {asm_b["cached"]!r}'
            )
            assert asm_b['token'] == tok_a, (
                f'expected tok_a={tok_a}, got {asm_b["token"]}'
            )
        finally:
            ws.close()


if __name__ == '__main__':
    run_tests(test_grabasm_cache_other_two_pipelines)
