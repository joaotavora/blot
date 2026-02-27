#!/usr/bin/env python3
"""Test JSONRPC session lifecycle: initialize and shutdown."""

from common import BlotServer, fixture_ccj, run_tests


CCJ = fixture_ccj("gcc-minimal")


def test_initialize_response():
    """initialize returns serverInfo, ccj, and project_root."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            result = endpoint.call("initialize", {})
        finally:
            endpoint.close()

    assert "serverInfo" in result, f"missing serverInfo: {result}"
    assert result["serverInfo"]["name"] == "blot", (
        f"unexpected server name: {result['serverInfo']}"
    )
    assert "ccj" in result, f"missing ccj: {result}"
    assert "project_root" in result, f"missing project_root: {result}"
    assert "gcc-minimal" in result["ccj"], (
        f"ccj does not reference fixture: {result['ccj']}"
    )


def test_shutdown():
    """shutdown returns an empty result object."""
    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        endpoint.call("initialize", {})
        result = endpoint.call("shutdown", {})
        endpoint.close()

    # Result should be an empty dict (our server returns {})
    assert isinstance(result, dict), f"shutdown result not a dict: {result!r}"


def test_unknown_method():
    """Unknown method returns JSONRPC error -32601."""
    from common import JsonRpcError

    with BlotServer(CCJ) as srv:
        endpoint = srv.connect()
        try:
            endpoint.call("no_such_method", {})
            assert False, "expected JsonRpcError"
        except JsonRpcError as e:
            assert e.code == -32601, f"expected -32601, got {e.code}"
        finally:
            endpoint.close()


if __name__ == "__main__":
    run_tests(test_initialize_response, test_shutdown, test_unknown_method)
