#!/usr/bin/env python3
"""Test GET /api/files and GET /api/source"""
from web_tests_common import BlotServer, fixture_ccj, run_tests


CCJ = fixture_ccj('gcc-minimal')


def test_files_lists_source():
    """Files list includes source.cpp from gcc-minimal."""
    with BlotServer(CCJ) as srv:
        data = srv.http_get('/api/files')
    assert 'files' in data, f'missing files key: {data}'
    assert isinstance(data['files'], list), f'files not a list: {data}'
    assert 'source.cpp' in data['files'], \
        f'source.cpp not in files: {data["files"]}'


def test_source_content():
    """GET /api/source returns file content."""
    with BlotServer(CCJ) as srv:
        data = srv.http_get('/api/source?file=source.cpp')
    assert 'content' in data, f'missing content: {data}'
    assert 'int main' in data['content'], \
        f'expected "int main" in content, got: {data["content"]!r}'


def test_source_missing_param():
    """GET /api/source without ?file= returns 400."""
    with BlotServer(CCJ) as srv:
        status, _ = srv.http_get_raw('/api/source')
    assert status == 400, f'expected 400, got {status}'


def test_source_path_traversal():
    """GET /api/source with ../ traversal is rejected with 403."""
    with BlotServer(CCJ) as srv:
        status, _ = srv.http_get_raw('/api/source?file=../../etc/passwd')
    assert status == 403, f'expected 403 for path traversal, got {status}'


def test_source_not_found():
    """GET /api/source for a non-existent file returns 404."""
    with BlotServer(CCJ) as srv:
        status, _ = srv.http_get_raw('/api/source?file=does_not_exist.cpp')
    assert status == 404, f'expected 404, got {status}'


if __name__ == '__main__':
    run_tests(
        test_files_lists_source,
        test_source_content,
        test_source_missing_param,
        test_source_path_traversal,
        test_source_not_found,
    )
