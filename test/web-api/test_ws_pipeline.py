#!/usr/bin/env python3
"""
Test the full blot/infer → blot/grab_asm → blot/annotate pipeline over WS.
"""

from web_tests_common import BlotServer, fixture_ccj, run_tests


CCJ = fixture_ccj('gcc-minimal')


def test_full_pipeline():
    """infer→grab_asm→annotate returns assembly and line_mappings."""
    with BlotServer(CCJ) as srv:
        ws = srv.ws_connect()
        try:
            ws.call('initialize', {})

            # Phase 1 – infer
            infer_res = ws.call('blot/infer', {'file': 'source.cpp'})
            assert 'token' in infer_res, f'infer: missing token: {infer_res}'
            assert 'inference' in infer_res, (
                f'infer: missing inference: {infer_res}'
            )
            inf = infer_res['inference']
            assert 'annotation_target' in inf, (
                'infer: missing annotation_target'
            )
            assert 'compilation_command' in inf, (
                'infer: missing compilation_command'
            )
            assert 'compilation_directory' in inf, (
                'infer: missing compilation_directory'
            )
            assert infer_res['cached'] is False, (
                f'first infer should not be cached: {infer_res["cached"]}'
            )

            # Phase 2 – grab_asm
            asm_res = ws.call('blot/grab_asm', {'token': infer_res['token']})
            assert 'token' in asm_res, f'grab_asm: missing token: {asm_res}'
            assert 'compilation_command' in asm_res, (
                f'grab_asm: missing compilation_command: {asm_res}'
            )
            assert asm_res['cached'] is False, (
                f'first grab_asm should not be cached: {asm_res["cached"]}'
            )

            # Phase 3 – annotate
            ann_res = ws.call(
                'blot/annotate',
                {
                    'token': asm_res['token'],
                    'options': {'demangle': True},
                },
            )
            assert 'assembly' in ann_res, (
                f'annotate: missing assembly: {ann_res}'
            )
            assert 'line_mappings' in ann_res, (
                f'annotate: missing line_mappings: {ann_res}'
            )
            assert isinstance(ann_res['assembly'], list), (
                'assembly should be a list'
            )
            assert len(ann_res['assembly']) > 0, 'assembly should be non-empty'
            assert isinstance(ann_res['line_mappings'], list), (
                'line_mappings should be a list'
            )
            assert ann_res['cached'] is False, (
                f'first annotate should not be cached: {ann_res["cached"]}'
            )
        finally:
            ws.close()


def test_progress_notifications():
    """Each pipeline phase emits running then done/cached progress notifications."""
    with BlotServer(CCJ) as srv:
        ws = srv.ws_connect()
        try:
            ws.call('initialize', {})

            ws.call('blot/infer', {'file': 'source.cpp'})
            infer_notifs = ws.pop_notifications()

            phases = [n['params']['phase'] for n in infer_notifs]
            statuses = [n['params']['status'] for n in infer_notifs]
            assert phases == ['infer', 'infer'], (
                f'expected 2 infer notifications, got: {infer_notifs}'
            )
            assert statuses[0] == 'running', (
                f'first infer notification should be running: {statuses}'
            )
            assert statuses[1] in ('done', 'cached', 'error'), (
                f'second infer notification unexpected: {statuses}'
            )
            assert 'elapsed_ms' not in infer_notifs[0]['params'], (
                'running notification should not have elapsed_ms'
            )
            assert 'elapsed_ms' in infer_notifs[1]['params'], (
                'done notification should have elapsed_ms'
            )
        finally:
            ws.close()


def test_infer_unknown_file():
    """infer for a file not in the CCJ returns a JSONRPC error."""
    from web_tests_common import JsonRpcError

    with BlotServer(CCJ) as srv:
        ws = srv.ws_connect()
        try:
            ws.call('initialize', {})
            try:
                ws.call('blot/infer', {'file': 'no_such_file.cpp'})
                assert False, 'expected JsonRpcError'
            except JsonRpcError as e:
                assert e.code in (-32602, -32603), (
                    f'unexpected error code: {e.code}'
                )
        finally:
            ws.close()


def test_annotate_with_options():
    """demangle=True and preserve_directives=True are accepted."""
    with BlotServer(CCJ) as srv:
        ws = srv.ws_connect()
        try:
            ws.call('initialize', {})
            infer_res = ws.call('blot/infer', {'file': 'source.cpp'})
            asm_res = ws.call('blot/grab_asm', {'token': infer_res['token']})
            ann_res = ws.call(
                'blot/annotate',
                {
                    'token': asm_res['token'],
                    'options': {
                        'demangle': True,
                        'preserve_directives': True,
                        'preserve_comments': False,
                    },
                },
            )
            assert 'assembly' in ann_res
            assert len(ann_res['assembly']) > 0
        finally:
            ws.close()


if __name__ == '__main__':
    run_tests(
        test_full_pipeline,
        test_progress_notifications,
        test_infer_unknown_file,
        test_annotate_with_options,
    )
