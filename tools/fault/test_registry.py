# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Stable IDs, compatibility, deterministic output and declaration coverage."""
from copy import deepcopy
import json
from pathlib import Path
import re
import unittest

import generate
import decode


class Registry(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((generate.ROOT/generate.REGISTRY).read_text())

    def test_checked_in_outputs_are_current(self):
        for path, expected in generate.outputs(self.data).items():
            self.assertEqual((generate.ROOT/path).read_text(), expected)

    def test_explicit_ids_do_not_depend_on_input_order(self):
        reordered = deepcopy(self.data)
        reordered['apis'].reverse()
        self.assertEqual(generate.outputs(self.data), generate.outputs(reordered))

    def test_zero_duplicates_and_bad_ranges_are_rejected(self):
        for value in (0, -1, 65536, True, 1):
            bad = deepcopy(self.data)
            bad['apis'][1]['id'] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                generate.validate(bad)
        bad = deepcopy(self.data)
        bad['apis'][1]['name'] = bad['apis'][0]['name']
        with self.assertRaises(ValueError):
            generate.validate(bad)

    def test_every_documented_public_function_has_an_id(self):
        functions = set()
        for header in (generate.ROOT/'include/emb').glob('*.h'):
            functions.update(re.findall(r'\* (emb_[a-z0-9_]+)\(\) -', header.read_text()))
        active = {api['name'] for api in generate.validate(self.data) if api['state'] == 'active'}
        self.assertEqual(functions, active)
        self.assertEqual(next(api['id'] for api in self.data['apis']
                              if api['name'] == 'emb_mutex_lock'), 0x0016)

    def test_published_pairs_cannot_be_deleted_renumbered_or_reassigned(self):
        for change in ('remove', 'renumber', 'rename'):
            new = deepcopy(self.data)
            if change == 'remove':
                new['apis'].pop(0)
            elif change == 'renumber':
                new['apis'][0]['id'] = 60000
            else:
                new['apis'][0]['name'] = 'emb_replacement'
            with self.subTest(change=change), self.assertRaises(ValueError):
                generate.compatible(self.data, new)

    def test_retirement_keeps_id_and_new_assignments_are_allowed(self):
        new = deepcopy(self.data)
        new['apis'][0]['state'] = 'retired'
        new['apis'].append(dict(id=60000, name='emb_future', header='future.h', state='active'))
        generate.compatible(self.data, new)
        with self.assertRaises(ValueError):
            generate.compatible(new, self.data)

    def test_decoder_uses_exported_numbers_and_reports_unknown(self):
        data = json.loads((generate.ROOT/generate.DECODER).read_text())
        self.assertEqual(decode.lookup(data, 0x0016), 'emb_mutex_lock')
        self.assertIsNone(decode.lookup(data, 0))
        self.assertIsNone(decode.lookup(data, 65535))

    def test_api_fault_sites_use_explicit_attribution(self):
        for path in (generate.ROOT/'kernel').rglob('*.c'):
            source = path.read_text()
            self.assertNotRegex(source, r'embk_fault_raise\(\s*EMB_FAULT_API_')
            self.assertNotRegex(source, r'\bEMBK_(?:REQUIRE|MISUSE)\(')
