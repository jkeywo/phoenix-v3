import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import test from "node:test";

const capture = fileURLToPath(new URL("../../scripts/wasm-trace.mjs", import.meta.url));

test("WASM trace capture preserves a large Unicode line through a pipe", () => {
	const trace = JSON.stringify({ payload: "trace-船-".repeat(20000) }) + "\n";
	const result = spawnSync(
		process.execPath,
		[
			capture,
			"--input-type=module",
			"-e",
			String.raw`import { writeSync } from 'node:fs'; writeSync(1, JSON.stringify({ payload: 'trace-船-'.repeat(20000) }) + '\n');`,
		],
		{
			encoding: "utf8",
			maxBuffer: 1024 * 1024,
		},
	);
	assert.equal(result.status, 0, result.stderr);
	assert.equal(result.stdout, trace);
});

test("WASM trace capture retains diagnostics and a failing executable's exit status", () => {
	const result = spawnSync(
		process.execPath,
		[capture, "-e", "console.log('partial trace'); console.error('synthetic failure'); process.exitCode = 7;"],
		{
			encoding: "utf8",
		},
	);
	assert.equal(result.status, 7);
	assert.equal(result.stdout, "partial trace\n");
	assert.match(result.stderr, /synthetic failure/);
});
