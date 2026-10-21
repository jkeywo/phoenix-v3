"""Synthetic HTTP provider boundary; no model download, CUDA or paid API needed."""
import importlib.util
import json
from pathlib import Path
import threading
import unittest
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

    def test_configuration_refuses_moving_model_and_unknown_provider(self):
        with self.assertRaises(ValueError):
            providers.Jev({"model": "jev-latest"})
        with self.assertRaises(ValueError):
            providers.Jev({"model": "jev-unversioned"})
        with self.assertRaises(ValueError):
            providers.load_providers({"unrecognised": {}})


if __name__ == "__main__":
    unittest.main()
