#!/usr/bin/env python3
"""Regression tests for customer edit-admission inventory parsing."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('check-customer-edit-admission.py')
SPEC = importlib.util.spec_from_file_location('customer_admission_audit', SCRIPT)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def fixture_root(entries, implementation):
    root = Path(tempfile.mkdtemp(prefix='looksy-admission-test-'))
    source = root / 'apps/desktop/src'
    source.mkdir(parents=True)
    (source / 'editor_preview_controller.h').write_text(
        'class EditorPreviewController {\n'
        '  Q_INVOKABLE bool edit();\n'
        '  Q_INVOKABLE bool read();\n'
        '};\n'
    )
    (source / 'editor_preview_controller.cpp').write_text(implementation)
    manifest = root / 'release/customer-edit-admission.json'
    manifest.parent.mkdir()
    manifest.write_text(json.dumps({'editor_preview_controller': entries}))
    return root


class AdmissionAuditTests(unittest.TestCase):
    def test_real_inventory_covers_all_current_photo_and_timeline_apis(self):
        self.assertEqual(AUDIT.audit(), [])

    def test_balanced_parser_accepts_nested_body_and_ignores_comment_fake_guard(self):
        root = fixture_root(
            {'edit': 'edit', 'read': 'read'},
            'bool EditorPreviewController::edit() {\n'
            '  const auto access = customer_edit_access_.enter();\n'
            '  if (!access) return false;\n'
            '  if (true) {\n'
            '    applyDocumentChange();\n'
            '  }\n'
            '  return true;\n'
            '}\n'
            'bool EditorPreviewController::read() {\n'
            '  // customer_edit_access_.enter() is only an example in this comment.\n'
            '  return true;\n'
            '}\n'
        )
        self.assertEqual(AUDIT.audit(root), [])

    def test_rejects_removed_direct_guard(self):
        root = fixture_root(
            {'edit': 'edit'},
            'bool EditorPreviewController::edit() {\n'
            '  document_ = replacement;\n'
            '  return true;\n'
            '}\n',
        )
        errors = AUDIT.audit(root)
        self.assertTrue(any('missing edit admission' in error for error in errors), errors)

    def test_rejects_member_mutation_before_guard(self):
        root = fixture_root(
            {'edit': 'edit'},
            'bool EditorPreviewController::edit() {\n'
            '  document_ = replacement;\n'
            '  const auto access = customer_edit_access_.enter();\n'
            '  if (!access) return false;\n'
            '  return true;\n'
            '}\n',
        )
        errors = AUDIT.audit(root)
        self.assertTrue(any('mutation may occur before' in error for error in errors), errors)

    def test_rejects_delegate_after_helper_guard_is_removed(self):
        root = fixture_root(
            {'edit': {'mode': 'edit', 'delegates': ['guardedHelper']}},
            'bool EditorPreviewController::edit() { return guardedHelper(); }\n'
            'bool EditorPreviewController::guardedHelper() {\n'
            '  document_ = replacement;\n'
            '  return true;\n'
            '}\n',
        )
        errors = AUDIT.audit(root)
        self.assertTrue(any('delegate target has no direct admission guard' in error for error in errors), errors)

    def test_rejects_delegate_cycles(self):
        root = fixture_root(
            {
                'edit': {'mode': 'edit', 'delegates': ['helper']},
                'helper': {'mode': 'edit', 'delegates': ['edit']},
            },
            'bool EditorPreviewController::edit() { return helper(); }\n'
            'bool EditorPreviewController::helper() { return edit(); }\n',
        )
        errors = AUDIT.audit(root)
        self.assertTrue(any('delegate cycle' in error for error in errors), errors)

    def test_read_save_and_export_exemptions_need_no_guard(self):
        root = fixture_root(
            {'edit': 'read', 'read': 'read'},
            'bool EditorPreviewController::edit() { exportExistingWork(); return true; }\n'
            'bool EditorPreviewController::read() { return true; }\n',
        )
        self.assertEqual(AUDIT.audit(root), [])


if __name__ == '__main__':
    unittest.main()
