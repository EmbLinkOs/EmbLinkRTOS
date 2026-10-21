#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Decode a numeric fault API ID using an exported registry (no firmware strings)."""
import argparse
import json
from pathlib import Path
import sys


def lookup(data, api_id):
    if data.get('schema_version') != 1:
        raise ValueError('unsupported API decoder schema')
    return next((api['name'] for api in data['apis'] if api['id'] == api_id), None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('api_id', type=lambda text: int(text, 0), help='decimal or 0x-prefixed ID')
    parser.add_argument('--map', type=Path, default=Path(__file__).with_name('api-ids.json'))
    args = parser.parse_args()
    if args.api_id == 0:
        print('0x0000: no API attribution')
        return 0
    name = lookup(json.loads(args.map.read_text()), args.api_id)
    print(f"0x{args.api_id:04X}: {name or 'unknown API ID'}")
    return 0 if name else 1


if __name__ == '__main__':
    sys.exit(main())
