#!/usr/bin/env python3
"""Emit the KMI matrix JSON for the build workflow.

Kept as a file rather than inline in the workflow: a multi-line python3 -c
inside a YAML block scalar has to be indented consistently, and getting that
wrong turns the python body into YAML keys.

Usage: make_matrix.py "<comma,separated,kmis>"
"""
import json
import sys

DEFAULT_KMIS = [
    'android12-5.10',
    'android13-5.10',
    'android13-5.15',
    'android14-5.15',
    'android14-6.1',
    'android15-6.6',
    'android16-6.12',
]

raw = (sys.argv[1] if len(sys.argv) > 1 else '').strip()
if raw:
    items = [k.strip() for k in raw.split(',') if k.strip()]
else:
    items = DEFAULT_KMIS

print(json.dumps({'kmi': items}))
