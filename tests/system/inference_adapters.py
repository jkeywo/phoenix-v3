"""Synthetic HTTP provider boundary; no model download, CUDA or paid API needed."""
import importlib.util
import json
from pathlib import Path
import threading
import unittest
import sys
from types import SimpleNamespace
from unittest.mock import patch
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

spec = importlib.util.spec_from_file_location("providers", Path(__file__).parents[2] / "tools/inference/providers.py")
providers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(providers)


class ProviderIntegration(unittest.TestCase):
    def test_constrained_local_decision_request(self):
        received = []
        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                received.append(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
                body = json.dumps({"choices": [{"message": {"content": '{"candidate":"stop"}'}}]}).encode()
                self.send_response(200)
                self.end_headers()
                self.wfile.write(body)
            def log_message(self, *_args):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            provider = providers.LlamaCpp({"model": "synthetic", "endpoint": f"http://127.0.0.1:{server.server_port}/v1/chat/completions"})
            snapshot = {"facts": {"temperature": "critical"}, "candidates": [
                {"id": "stop", "description": "Stop"}, {"id": "continue", "description": "Continue"}]}
            self.assertEqual(provider.choose(snapshot), "stop")
            schema = received[0]["response_format"]["json_schema"]["schema"]
            self.assertEqual(schema["properties"]["candidate"]["enum"], ["stop", "continue"])
            self.assertIn('"temperature": "critical"', received[0]["messages"][0]["content"])
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_batched_choices_preserve_separate_console_states_and_id_mapping(self):
        seen = []
        class Decider:
            def decide_batch(self, states, question):
                seen.extend(json.loads(s)["state"] for s in states)
                return [SimpleNamespace(distribution={"candidate-0": 0.1, "candidate-1": 0.9}) for _ in states]
        question = SimpleNamespace(choice=lambda *_args, **_kwargs: None)
        provider = providers.AnyJev.__new__(providers.AnyJev)
        provider.make_decider = Decider
        provider.deciders = {}
        provider.backend = providers.CoalescingBackend(SimpleNamespace())
        snapshots = [{"facts": {"private": owner}, "candidates": [
            {"id": owner + "-stop"}, {"id": owner + "-continue"}]} for owner in ["helm", "engineering"]]
        with patch.dict(sys.modules, {"anyjev": SimpleNamespace(Question=question)}):
            choices = provider.choose_many(snapshots)
        self.assertEqual(choices, ["helm-continue", "engineering-continue"])
        self.assertEqual([row["facts"]["private"] for row in seen], ["helm", "engineering"])
        self.assertNotIn("engineering", json.dumps(seen[0]))
        self.assertEqual(snapshots[0]["candidates"][0]["id"], "helm-stop")

    def test_queries_share_one_model_thread_and_preserve_reply_order(self):
        owner = threading.get_ident()
        calls = []
        class Backend:
            def score_shared(self, groups, ids):
                calls.append((threading.get_ident(), groups))
                return groups
        backend = providers.CoalescingBackend(Backend())
        result = backend.run(["helm", "engineering", "cooling"],
                             lambda name: backend.score_shared([name], [[0, 1]])[0])
        self.assertEqual(result, ["helm", "engineering", "cooling"])
        self.assertTrue(all(thread == owner for thread, _ in calls))
        self.assertEqual(sum(len(groups) for _, groups in calls), 3)

    def test_fact_context_keeps_latest_provenance_and_separate_sources(self):
        old = {"kind": 0, "schema": "energy", "source": 1, "subject": 3, "publication": 0,
               "observed_tick": 1, "fields": {"stored": 100}, "uncertainty": 2, "withdrawn": False}
        latest = dict(old, observed_tick=2, fields={"stored": 90}, uncertainty=3, withdrawn=True)
        other = dict(latest, source=2, fields={"stored": 50})
        claim = dict(old, kind=1)
        snapshot = {"facts": {"energy": 90, "energy.source": 1, "energy.observed_tick": 2, "energy.uncertainty": 3},
                    "candidates": [], "evidence": {"records": [old, latest, other, claim]}}
        state = providers.context(snapshot)["state"]
        self.assertEqual(len(state["evidence"]["records"]), 3)
        fact = next(r for r in state["evidence"]["records"] if r["kind"] == 0 and r["source"] == 1)
        self.assertTrue(fact["withdrawn"])
        self.assertEqual(fact["uncertainty"], 3)
        self.assertEqual(fact["earlier_retained_sample"]["fields"]["stored"], 100)
        self.assertEqual(len(snapshot["evidence"]["records"]), 4)
        self.assertEqual(state["facts"]["energy"], 90)
        self.assertEqual(state["observation_evidence"][0]["source"], 1)
        self.assertEqual(state["observation_evidence"][0]["uncertainty"], 3)
        self.assertIn("energy.source", snapshot["facts"])

    def test_configuration_refuses_moving_model_and_unknown_provider(self):
        with self.assertRaises(ValueError):
            providers.Jev({"model": "jev-latest"})
        with self.assertRaises(ValueError):
            providers.Jev({"model": "jev-unversioned"})
        with self.assertRaises(ValueError):
            providers.load_providers({"unrecognised": {}})


if __name__ == "__main__":
    unittest.main()
