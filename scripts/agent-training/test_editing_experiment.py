#!/usr/bin/env python3
"""Check corpus/metric contracts without loading model weights or using the GPU."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + ".py"))
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


prepare = module("prepare_editing_data")
experiment = module("gemma_editing_experiment")


class EditingDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = {t["name"]: t for t in json.loads(Path(
            "training/gemma4-editing/tool-catalog.json").read_text())["tools"]}

    def case(self, rule):
        return prepare.make_case(rule, "test", 0, self.catalog)

    def test_all_workflow_templates_match_native_schemas(self):
        for rule in prepare.RULES:
            for split in ("train", "valid", "test"):
                with self.subTest(rule=rule, split=split):
                    prepare.validate_case(prepare.make_case(rule, split, 0, self.catalog), self.catalog)

    def test_known_actions_are_accepted(self):
        for rule in prepare.RULES:
            row = self.case(rule)
            self.assertTrue(experiment.score_response(row["messages"][-1]["content"], row, self.catalog)[0])

    def test_guessed_ids_are_rejected(self):
        row = self.case("typography")
        answer = json.loads(row["messages"][-1]["content"])
        answer["toolCalls"][0]["arguments"]["layerId"] = "invented-layer"
        self.assertFalse(experiment.score_response(json.dumps(answer), row, self.catalog)[0])

    def test_extra_mutation_is_not_hidden_by_a_valid_first_call(self):
        row = self.case("text")
        answer = json.loads(row["messages"][-1]["content"])
        answer["toolCalls"].append(copy.deepcopy(answer["toolCalls"][0]))
        self.assertFalse(experiment.score_response(json.dumps(answer), row, self.catalog)[0])

    def test_invalid_json_and_schema_are_rejected(self):
        row = self.case("transform")
        answer = json.loads(row["messages"][-1]["content"])
        del answer["toolCalls"][0]["arguments"]["values"]["rotation"]
        self.assertFalse(experiment.score_response(json.dumps(answer), row, self.catalog)[0])
        self.assertFalse(experiment.score_response("Here is your answer: {}", row, self.catalog)[0])

    def test_splits_have_distinct_prompts_and_resource_ids(self):
        prompts = set()
        ids = set()
        for split in ("train", "valid", "test"):
            for rule in prepare.RULES:
                row = prepare.make_case(rule, split, 0, self.catalog)
                prompt = row["messages"][1]["content"]
                self.assertNotIn(prompt, prompts)
                prompts.add(prompt)
                asset = json.loads(prompt)["state"]["assetId"]
                self.assertNotIn(asset, ids)
                ids.add(asset)


if __name__ == "__main__":
    unittest.main()
