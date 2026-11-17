#!/usr/bin/env python3
"""CPU contracts for decision replay and probability validation."""
import copy
import json
import math
from pathlib import Path
import tempfile
import unittest

from evaluate_decisions import choice_schema, systemone_request, validate_choice, validate_frame, verify_loader, load_records


class DecisionContracts(unittest.TestCase):
    def setUp(self):
        self.rows = json.loads(Path("training/agent-decisions/frames.json").read_text())
        self.frame = self.rows[0]["frame"]

    def test_native_frame_digest_parity_and_labels_outside_requests(self):
        self.assertEqual(len({r["id"] for r in self.rows}), 24)
        native_tools = {t["name"] for t in json.loads(Path("training/gemma4-editing/tool-catalog.json").read_text())["tools"]}
        for row in self.rows:
            ids = validate_frame(row["frame"])
            self.assertTrue(set(row["acceptableChoices"]) <= set(ids))
            request = systemone_request(row["frame"])
            self.assertNotIn("acceptableChoices", json.dumps(request))
            self.assertNotIn("labelStatus", json.dumps(request))
            self.assertEqual(set(request["questions"]["next_step"]["criteria"]), set(ids))
            self.assertTrue({t for c in row["frame"]["candidates"] for t in c.get("toolNames", [])} <= native_tools)

    def test_stale_and_modified_inputs_rejected(self):
        changed = copy.deepcopy(self.frame)
        changed["state"]["brief"] = "new brief"
        with self.assertRaises(ValueError):
            validate_frame(changed)

    def test_only_current_choices_allowed(self):
        response = validate_choice(self.frame, "insufficient_evidence")
        self.assertEqual(response["requestDigest"], self.frame["requestDigest"])
        with self.assertRaises(ValueError):
            validate_choice(self.frame, "invented_delete")
        schema = choice_schema(self.frame)
        self.assertFalse(schema["additionalProperties"])
        self.assertEqual(schema["required"], ["choice"])

    def test_probabilities_require_complete_finite_distribution(self):
        ids = validate_frame(self.frame)
        probabilities = {key: 1 / len(ids) for key in ids}
        validate_choice(self.frame, ids[0], probabilities)
        for invalid in ({}, {**probabilities, ids[0]: math.nan},
                        {**probabilities, ids[0]: -0.1}, {**probabilities, ids[0]: True},
                        {key: 0 for key in ids}):
            with self.assertRaises(ValueError):
                validate_choice(self.frame, ids[0], invalid)

    def test_argmax_checked(self):
        ids = validate_frame(self.frame)
        probabilities = {key: 0 for key in ids}
        probabilities[ids[1]] = 1
        with self.assertRaises(ValueError):
            validate_choice(self.frame, ids[0], probabilities)

    def test_pinned_loader_required(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            (path / "clef_mlx.py").write_text("changed source")
            with self.assertRaises(ValueError):
                verify_loader(path, Path("training/agent-decisions/clef.lock.json"))

    def test_shadow_replay_is_unscored_and_excludes_original_behavior(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            capture = {"frame": self.frame, "observation": {"chosenFamilies": ["use_document_tools"]}}
            (path / "a.json").write_text(json.dumps(capture))
            (path / "duplicate.json").write_text(json.dumps(capture))
            records, digest = load_records(path)
            self.assertEqual(len(records), 1)
            self.assertIsNone(records[0]["acceptableChoices"])
            self.assertNotIn("observation", records[0])
            capture["observation"]["chosenFamilies"] = ["ask_user"]
            (path / "a.json").write_text(json.dumps(capture))
            self.assertEqual(load_records(path)[1], digest)
            (path / "duplicate.json").unlink()
            capture["review"] = {"acceptableChoices": ["inspect_state"]}
            (path / "a.json").write_text(json.dumps(capture))
            self.assertEqual(load_records(path)[0][0]["acceptableChoices"], ["inspect_state"])


if __name__ == "__main__":
    unittest.main()
