#!/usr/bin/env python3
"""
Explicit inference object matching a cached key returns cached='other'.

The client passes an 'inference' dict (compilation_command + directory) that
matches a key already in asm_cache_2 → cached='other', no recompile.
"""

from web_tests_common import BlotServer, fixture_ccj, run_tests


CCJ = fixture_ccj('gcc-minimal')


def test_grabasm_cache_other_inference_object():
    with BlotServer(CCJ) as srv:
        ws = srv.ws_connect()
        try:
            ws.call('initialize', {})

            # Populate asm_cache_2
            infer_res = ws.call('blot/infer', {'file': 'source.cpp'})
            tok = infer_res['token']
            asm_res = ws.call('blot/grab_asm', {'token': tok})
            assert asm_res['cached'] is False

            # Pass explicit inference object — same command+directory → asm_cache_2 hit
            inf = infer_res['inference']
            asm2 = ws.call(
                'blot/grab_asm',
                {
                    'inference': {
                        'compilation_command': inf['compilation_command'],
                        'compilation_directory': inf['compilation_directory'],
                        'annotation_target': inf['annotation_target'],
                    }
                },
            )
            assert asm2['cached'] == 'other', (
                f'expected cached="other", got {asm2["cached"]!r}'
            )
            assert asm2['token'] == tok, (
                f'token mismatch: {asm2["token"]} != {tok}'
            )
        finally:
            ws.close()


if __name__ == '__main__':
    run_tests(test_grabasm_cache_other_inference_object)
