"""End-to-end checks for the real executable, with isolated input/output files."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


if len(sys.argv) < 2:
    raise SystemExit("Usage: cli_smoke.py /absolute/path/to/search_engine")

EXECUTABLE = Path(sys.argv.pop(1)).resolve()


class CliSmokeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="search_engine_cli_")
        self.directory = Path(self.temporary.name) / "workspace with spaces"
        self.directory.mkdir()
        (self.directory / "resources").mkdir()

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def write_json(path, data):
        path.write_text(json.dumps(data, indent=2), encoding="utf-8")

    def configuration(self, files, **options):
        return {
            "config": {"name": "SmokeSearchEngine", "version": "0.1", **options},
            "files": files,
        }

    def prepare(self, documents, queries, **options):
        files = []
        for number, content in enumerate(documents):
            relative_path = f"resources/file{number:03}.txt"
            (self.directory / relative_path).write_text(content, encoding="ascii")
            files.append(relative_path)
        self.write_json(self.directory / "config.json", self.configuration(files, **options))
        self.write_json(self.directory / "requests.json", {"requests": queries})

    def run_engine(self, *arguments):
        return subprocess.run(
            [str(EXECUTABLE), *map(str, arguments)],
            cwd=self.directory,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=15,
            check=False,
        )

    def read_answers(self, filename="answers.json"):
        return json.loads((self.directory / filename).read_text(encoding="utf-8"))["answers"]

    def assert_success(self, process):
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)

    def test_default_paths_and_reference_ranking(self):
        self.prepare(
            ["milk milk milk milk water water water", "milk water water",
             "milk milk milk milk milk water water water water water", "americano cappuccino"],
            ["milk water", "sugar", "cappuccino"],
        )
        result = self.run_engine()
        self.assert_success(result)
        self.assertIn("Starting", result.stdout)
        self.assertIn("SmokeSearchEngine", result.stdout)
        answers = self.read_answers()
        self.assertEqual(list(answers), ["request001", "request002", "request003"])
        self.assertEqual(answers["request001"]["result"], "true")
        relevance = answers["request001"]["relevance"]
        self.assertEqual([entry["docid"] for entry in relevance], [2, 0, 1])
        for entry, rank in zip(relevance, [1.0, 0.7, 0.3]):
            self.assertAlmostEqual(entry["rank"], rank, places=6)
        self.assertEqual(answers["request002"], {"result": "false"})
        self.assertEqual(answers["request003"]["relevance"], [{"docid": 3, "rank": 1.0}])

    def test_all_and_explicit_any_modes(self):
        self.prepare(["milk milk", "milk water", "water water water"], ["milk water"])
        self.assert_success(self.run_engine())
        self.assertEqual([item["docid"] for item in self.read_answers()["request001"]["relevance"]], [1])

        self.assert_success(self.run_engine("--match", "any"))
        self.assertEqual([item["docid"] for item in self.read_answers()["request001"]["relevance"]], [2, 0, 1])

    def test_default_and_configured_result_limits(self):
        self.prepare(["milk"] * 8, ["milk"])
        self.assert_success(self.run_engine())
        self.assertEqual(len(self.read_answers()["request001"]["relevance"]), 5)

        config_path = self.directory / "config.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config["config"]["max_responses"] = 1
        self.write_json(config_path, config)
        self.assert_success(self.run_engine())
        self.assertEqual(self.read_answers()["request001"]["relevance"], [{"docid": 0, "rank": 1.0}])

    def test_missing_document_keeps_later_document_ids(self):
        self.prepare(["sugar", "milk"], ["milk"])
        (self.directory / "resources" / "file000.txt").unlink()
        process = self.run_engine()
        self.assert_success(process)
        self.assertTrue(process.stderr)
        self.assertEqual(self.read_answers()["request001"]["relevance"], [{"docid": 1, "rank": 1.0}])

    def test_custom_paths_and_config_relative_documents(self):
        inputs = self.directory / "custom inputs"
        inputs.mkdir()
        (inputs / "a.txt").write_text("milk milk", encoding="ascii")
        self.write_json(inputs / "settings.json", self.configuration(["a.txt"]))
        self.write_json(self.directory / "queries.json", {"requests": ["milk"]})

        process = self.run_engine("--config", "custom inputs/settings.json",
                                  "--requests", "queries.json", "--answers", "result file.json")
        self.assert_success(process)
        self.assertEqual(self.read_answers("result file.json")["request001"]["relevance"],
                         [{"docid": 0, "rank": 1.0}])
        self.assertFalse((self.directory / "answers.json").exists())

    def test_unicode_absolute_paths_and_document_names(self):
        inputs = self.directory / "данные с пробелами"
        inputs.mkdir()
        relative_document = inputs / "первый документ.txt"
        absolute_document = inputs / "второй документ.txt"
        relative_document.write_text("milk", encoding="ascii")
        absolute_document.write_text("milk milk", encoding="ascii")
        config = inputs / "настройки.json"
        requests = inputs / "запросы.json"
        answers = inputs / "ответы.json"
        self.write_json(config, self.configuration(
            [relative_document.name, str(absolute_document.resolve())]))
        self.write_json(requests, {"requests": ["milk"]})

        process = self.run_engine("--config", config.resolve(), "--requests", requests.resolve(),
                                  "--answers", answers.resolve())
        self.assert_success(process)
        relevance = json.loads(answers.read_text(encoding="utf-8"))["answers"]["request001"]["relevance"]
        self.assertEqual([entry["docid"] for entry in relevance], [1, 0])
        self.assertEqual([entry["rank"] for entry in relevance], [1.0, 0.5])
        self.assertFalse((self.directory / "answers.json").exists())

    def test_answers_cannot_overwrite_any_input(self):
        self.prepare(["milk"], ["milk"])
        inputs = [self.directory / "config.json", self.directory / "requests.json",
                  self.directory / "resources" / "file000.txt"]
        original_contents = {path: path.read_bytes() for path in inputs}

        for target in inputs:
            with self.subTest(target=target.name):
                process = self.run_engine("--answers", target.resolve())
                self.assertEqual(process.returncode, 1, process.stdout + process.stderr)
                for path, contents in original_contents.items():
                    self.assertEqual(path.read_bytes(), contents,
                                     f"Output collision modified input: {path}")
                self.assertFalse((self.directory / "answers.json").exists())

    def test_success_replaces_old_answer_contents(self):
        self.prepare(["milk"], ["milk"])
        answer_path = self.directory / "answers.json"
        answer_path.write_text("stale invalid contents" * 100, encoding="utf-8")
        self.assert_success(self.run_engine())
        self.assertEqual(len(self.read_answers()), 1)

        self.write_json(self.directory / "requests.json", {"requests": []})
        self.assert_success(self.run_engine())
        self.assertEqual(self.read_answers(), {})

    def test_missing_config_fails_with_the_required_message(self):
        process = self.run_engine()
        self.assertEqual(process.returncode, 1)
        self.assertIn("config file is missing", process.stderr + process.stdout)
        self.assertFalse((self.directory / "answers.json").exists())

    def test_invalid_version_and_empty_config_are_reported(self):
        self.prepare(["milk"], ["milk"])
        config_path = self.directory / "config.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        config["config"]["version"] = "wrong"
        self.write_json(config_path, config)
        process = self.run_engine()
        self.assertEqual(process.returncode, 1)
        self.assertIn("config.json has incorrect file version", process.stderr + process.stdout)

        self.write_json(config_path, {"files": []})
        process = self.run_engine()
        self.assertEqual(process.returncode, 1)
        self.assertIn("config file is empty", process.stderr + process.stdout)

    def test_invalid_requests_fail_without_creating_answers(self):
        self.prepare(["milk"], ["milk"])
        for requests in ({"requests": ["Milk"]}, {"requests": ["a b c d e f g h i j k"]},
                         {"requests": "milk"}):
            self.write_json(self.directory / "requests.json", requests)
            with self.subTest(requests=requests):
                process = self.run_engine()
                self.assertEqual(process.returncode, 1)
                self.assertFalse((self.directory / "answers.json").exists())

    def test_help_and_invalid_arguments(self):
        process = self.run_engine("--help")
        self.assert_success(process)
        self.assertIn("--config", process.stdout)
        for arguments in (("--unknown",), ("--config",), ("--match", "invalid")):
            with self.subTest(arguments=arguments):
                self.assertEqual(self.run_engine(*arguments).returncode, 1)

    def test_watch_reindexes_changed_documents_and_requests(self):
        self.prepare(["milk"], ["milk"], update_interval=1)
        process = subprocess.Popen(
            [str(EXECUTABLE), "--watch"], cwd=self.directory,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        try:
            def wait_for_result(expected):
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        output, errors = process.communicate()
                        self.fail(f"Watch stopped: {output!r} {errors!r}")
                    try:
                        if self.read_answers()["request001"]["result"] == expected:
                            return
                    except (OSError, ValueError, KeyError):
                        pass
                    time.sleep(0.05)
                self.fail(f"Watch did not produce result={expected!r}")

            wait_for_result("true")
            document = self.directory / "resources" / "file000.txt"
            replacement = document.with_suffix(".new")
            replacement.write_text("water", encoding="ascii")
            replacement.replace(document)
            wait_for_result("false")

            requests = self.directory / "requests.json"
            replacement_requests = requests.with_suffix(".new")
            self.write_json(replacement_requests, {"requests": ["water"]})
            replacement_requests.replace(requests)
            wait_for_result("true")
        finally:
            process.terminate()
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
