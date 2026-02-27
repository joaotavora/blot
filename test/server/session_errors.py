#!/usr/bin/env python3
"""Test JSONRPC error handling."""

from common import (
    BlotServer,
    JsonRpcError,
    fixture_ccj,
    run_tests,
)


CCJ = fixture_ccj('gcc-minimal')


def _assert_rpc_error(endpoint, method, params, expected_code):
    try:
        endpoint.call(method, params)
        assert False, f'{method}: expected JsonRpcError but call succeeded'
    except JsonRpcError as e:
        assert e.code == expected_code, (
            f'{method}: expected error {expected_code}, got {e.code}: {e.message}'
        )


def test_unknown_method():
    """Calling an unknown method returns error -32601."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            _assert_rpc_error(endpoint, 'blot/no_such_method', {}, -32601)
        finally:
            endpoint.close()


def test_infer_missing_params():
    """blot/infer with neither 'file' nor 'token' returns -32602."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            _assert_rpc_error(endpoint, 'blot/infer', {}, -32602)
        finally:
            endpoint.close()


def test_infer_path_traversal():
    """blot/infer with a traversal path is rejected with -32602."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            _assert_rpc_error(
                endpoint, 'blot/infer', {'file': '../../etc/passwd'}, -32602
            )
        finally:
            endpoint.close()


def test_infer_stale_token():
    """blot/infer with a token that never existed returns -32602."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            _assert_rpc_error(endpoint, 'blot/infer', {'token': 999999}, -32602)
        finally:
            endpoint.close()


def test_grabasm_missing_params():
    """blot/grab_asm with neither 'inference' nor 'token' returns -32602."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            _assert_rpc_error(endpoint, 'blot/grab_asm', {}, -32602)
        finally:
            endpoint.close()


def test_grabasm_stale_token():
    """blot/grab_asm with an unknown token returns -32602."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            _assert_rpc_error(endpoint, 'blot/grab_asm', {'token': 999999}, -32602)
        finally:
            endpoint.close()


def test_annotate_missing_params():
    """blot/annotate with neither 'token' nor 'asm_blob' returns -32602."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            _assert_rpc_error(endpoint, 'blot/annotate', {}, -32602)
        finally:
            endpoint.close()


def test_annotate_stale_token():
    """blot/annotate with an unknown token returns -32602."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            _assert_rpc_error(endpoint, 'blot/annotate', {'token': 999999}, -32602)
        finally:
            endpoint.close()


def test_errors_do_not_break_session():
    """After a series of errors the session remains functional."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            # Trigger several errors
            for _ in range(3):
                try:
                    endpoint.call('blot/infer', {})
                except JsonRpcError:
                    pass

            # Session should still work for a valid request
            result = endpoint.call('initialize', {})
            assert 'serverInfo' in result, (
                f'session broken after errors: {result}'
            )
        finally:
            endpoint.close()


if __name__ == '__main__':
    run_tests(
        test_unknown_method,
        test_infer_missing_params,
        test_infer_path_traversal,
        test_infer_stale_token,
        test_grabasm_missing_params,
        test_grabasm_stale_token,
        test_annotate_missing_params,
        test_annotate_stale_token,
        test_errors_do_not_break_session,
    )
