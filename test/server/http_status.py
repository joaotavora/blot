#!/usr/bin/env python3
"""Test GET /api/status"""
import os
from common import BlotServer, fixture_ccj, run_tests


CCJ = fixture_ccj('gcc-minimal')


def test_status_fields():
    """Response has tu_count, ccj and project_root."""
    with BlotServer(CCJ) as srv:
        data = srv.http_get('/api/status')
    assert 'tu_count' in data, f'missing tu_count: {data}'
    assert 'ccj' in data, f'missing ccj: {data}'
    assert 'project_root' in data, f'missing project_root: {data}'


def test_status_tu_count():
    """gcc-minimal fixture has exactly 1 translation unit."""
    with BlotServer(CCJ) as srv:
        data = srv.http_get('/api/status')
    assert data['tu_count'] == 1, f'expected tu_count=1, got {data["tu_count"]}'


def test_status_paths():
    """ccj and project_root point into the fixture directory."""
    with BlotServer(CCJ) as srv:
        data = srv.http_get('/api/status')
    assert 'gcc-minimal' in data['ccj'], f'ccj path unexpected: {data["ccj"]}'
    assert os.path.isdir(data['project_root']), \
        f'project_root not a directory: {data["project_root"]}'


if __name__ == '__main__':
    run_tests(test_status_fields, test_status_tu_count, test_status_paths)
