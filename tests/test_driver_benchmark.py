from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def load(name: str, relative: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


run = load("run_driver_mvp2", "bench/run_driver_mvp2.py")
gate = load("evaluate_mvp2", "bench/evaluate_mvp2.py")
FIXTURE = ROOT / "bench/fixtures/driver-workloads.json"
SOURCES = {"python": "pysha:clean", "c": "csha:clean", "rust": "rustsha:clean"}
ADAPTERS = {"python": "py", "c": "c", "rust": "rust"}


class DriverBenchmarkTest(unittest.TestCase):
    def test_fixture_is_content_addressed(self):
        manifest = json.loads((ROOT / "bench/fixtures/manifest.json").read_bytes())
        self.assertEqual(
            run.sha256_file(FIXTURE), manifest["fixtures"]["driver-workloads"]["sha256"]
        )

    def adapter(self, command, fixture, case, concurrency, trial, namespace):
        data = json.loads(fixture.read_bytes())
        case_data = data["cases"][case]
        operations = case_data["operations"]
        tokens = [f"{iteration:06d}:{operation['id']}" for iteration in range(case_data["repeat"])
                  for operation in operations]
        return {
            "elapsed_ms": {"py": 10.0, "c": 5.0, "rust": 4.0}[command],
            "fixture_sha256": run.sha256_file(fixture),
            "completed_tokens": tokens,
            "operation_latency_ns": [1] * len(tokens),
            "terminal": case_data["terminal"],
            "result_sha256": case_data["terminal_sha256"],
            "errors": 0,
            "dropped": 0,
            "configuration": {
                "concurrency": concurrency,
                "operation_count": len(operations) * 1000,
                "pipeline_depth": 1,
                "timeout_ms": 5000,
                "event_loop_mode": {"py": "asyncio", "c": "lws", "rust": "tokio"}[command],
                "worker_count": 1,
                "queue_depth": 1,
                "connection_count": 1,
                "retry_policy": "none",
                "saturation_policy": "bounded_wait",
                "service_config_sha256": data["contract"]["service_config_sha256"],
                "schema_sha256": data["contract"]["schema_sha256"],
            },
            "resources": {"peak_rss_bytes": 100, "cpu_time_ms": 1.0},
            "build": {"runtime": command, "compiler": "test", "flags": "test",
                      "dependencies": {"driver": "1"}},
        }

    @mock.patch.object(run, "run_adapter")
    def test_runs_30_latin_rotated_trials_for_every_case_and_concurrency(self, adapter):
        adapter.side_effect = self.adapter
        records = run.execute(FIXTURE, ADAPTERS, SOURCES, 30)
        self.assertEqual(2 * 3 * 30 * 3, len(records))
        first_cell = [item for item in records if item["case"] == "redis-hot-path" and item["concurrency"] == 1]
        orders = []
        for trial in range(3):
            orders.append([item["variant"] for item in first_cell if item["trial"] == trial])
        self.assertEqual([
            ["python", "c", "rust"], ["c", "rust", "python"], ["rust", "python", "c"]
        ], orders)
        results = run.build_results(records, FIXTURE, SOURCES)
        self.assertEqual(18, len(results))
        self.assertTrue(all(doc["summary"]["sample_count"] == 30 for doc in results))
        self.assertTrue(all(doc["throughput_summary"]["sample_count"] == 30 for doc in results))
        self.assertEqual([0, 2, 1] * 10, results[1]["trial_order_index"])

    def test_requires_all_clean_variants_and_30_trials(self):
        with self.assertRaisesRegex(ValueError, "at least 30"):
            run.execute(FIXTURE, ADAPTERS, SOURCES, 29)
        with self.assertRaisesRegex(ValueError, "python,c,rust"):
            run.execute(FIXTURE, {"python": "py", "c": "c"}, SOURCES, 30)
        dirty = dict(SOURCES, c="csha:dirty")
        with self.assertRaisesRegex(ValueError, "COMMIT:clean"):
            run.execute(FIXTURE, ADAPTERS, dirty, 30)

    @mock.patch.object(run, "run_adapter")
    def test_smoke_allows_one_strict_trial_per_cell(self, adapter):
        adapter.side_effect = self.adapter
        records = run.execute(FIXTURE, ADAPTERS, SOURCES, 1, minimum_trials=1)
        self.assertEqual(2 * 3 * 3, len(records))

    @mock.patch.object(run, "run_adapter")
    def test_fails_closed_on_parity_errors_and_completed_id_order(self, adapter):
        def mismatch(*args):
            value = self.adapter(*args)
            if args[0] == "c":
                value["result_sha256"] = "0" * 64
            return value

        adapter.side_effect = mismatch
        with self.assertRaisesRegex(ValueError, "committed golden"):
            run.execute(FIXTURE, ADAPTERS, SOURCES, 30)

        def dropped(*args):
            value = self.adapter(*args)
            value["dropped"] = 1
            return value

        adapter.side_effect = dropped
        with self.assertRaisesRegex(ValueError, "errors/drops"):
            run.execute(FIXTURE, ADAPTERS, SOURCES, 30)

        def reordered(*args):
            value = self.adapter(*args)
            value["completed_tokens"].reverse()
            return value

        adapter.side_effect = reordered
        with self.assertRaisesRegex(ValueError, "completion tokens"):
            run.execute(FIXTURE, ADAPTERS, SOURCES, 30)

    @mock.patch.object(run, "run_adapter")
    def test_fails_closed_on_golden_config_latency_and_dependency_drift(self, adapter):
        mutations = (
            ("terminal", lambda value: value["terminal"].append({"wrong": True}), "committed golden"),
            ("latency", lambda value: value["operation_latency_ns"].pop(), "latency samples"),
            ("config", lambda value: value["configuration"].update({"pipeline_depth": 32}), "configuration mismatch"),
            ("loop", lambda value: value["configuration"].update({"event_loop_mode": "poll"}), "event_loop_mode"),
            ("deps", lambda value: value["build"].update({"dependencies": {}}), "dependency metadata"),
        )
        for _name, mutate, message in mutations:
            def invalid(*args, mutate=mutate):
                value = self.adapter(*args)
                mutate(value)
                return value
            adapter.side_effect = invalid
            with self.assertRaisesRegex(ValueError, message):
                run.execute(FIXTURE, ADAPTERS, SOURCES, 30)

    def test_adapter_attestation_hashes_command_and_measured_file(self):
        command = f"python3 {ROOT / 'bench/adapters/python_driver.py'}"
        first = run.adapter_attestation(command)
        second = run.adapter_attestation(command)
        self.assertEqual(first, second)
        self.assertEqual(run.sha256_file(ROOT / "bench/adapters/python_driver.py"),
                         first["artifact_sha256"])

    def test_bootstrap_is_deterministic_and_gate_requires_every_cell(self):
        first = gate.bootstrap_speedup([10.0] * 30, [5.0] * 30)
        second = gate.bootstrap_speedup([10.0] * 30, [5.0] * 30)
        self.assertEqual(first, second)
        self.assertEqual({"speedup": 2.0, "ci95_low": 2.0, "ci95_high": 2.0}, first)

        docs = []
        with tempfile.TemporaryDirectory() as tmp:
            for case in run.CASES:
                for concurrency in run.CONCURRENCIES:
                    for variant, samples in (("python", [10.0] * 30), ("c", [5.0] * 30)):
                        doc = {
                            "case": case, "variant": variant, "eligible": True,
                            "parity": {"passed": True}, "source": {"dirty": False},
                            "workload": {"concurrency": concurrency, "fixture_sha256": "same"},
                            "samples_ms": samples, "errors": {"count": 0, "dropped": 0},
                            "resources": {
                                "peak_rss_bytes": 100 if variant == "python" else 80,
                                "cpu_time_ms": 100 if variant == "python" else 80,
                            },
                        }
                        path = Path(tmp) / f"{case}-{concurrency}-{variant}.json"
                        path.write_text(json.dumps(doc))
                        docs.append(path)
            self.assertEqual("GO", gate.evaluate(docs, "c")["decision"])
            last = json.loads(docs[-1].read_text())
            last["resources"]["peak_rss_bytes"] = 101
            docs[-1].write_text(json.dumps(last))
            self.assertEqual("NO-GO", gate.evaluate(docs, "c")["decision"])
            docs.pop()
            with self.assertRaisesRegex(ValueError, "missing result"):
                gate.evaluate(docs, "c")


if __name__ == "__main__":
    unittest.main()
