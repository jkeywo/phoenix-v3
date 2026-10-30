import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync, unlinkSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { continuationCases, compareContinuations, recordContinuations } from "../../scripts/wasm-continuations.mjs";

test("recorded artifacts preserve large process output and propagate executable failure", (t) => {
	t.mock.method(console, "log", () => {});
	const directory = mkdtempSync(join(tmpdir(), "phoenix trace recording "));
	try {
		const web = join(directory, "web");
		const native = join(directory, "native");
		const output = join(directory, "recorded");
		mkdirSync(web);
		mkdirSync(native);
		for (const [trace, executable] of continuationCases) {
			writeFileSync(
				join(web, executable),
				`require('node:fs').writeSync(1, JSON.stringify({ executable: ${JSON.stringify(executable)}, payload: '船'.repeat(40000) }) + '\\n');`,
			);
			writeFileSync(join(native, trace), JSON.stringify({ executable, payload: "船".repeat(40000) }));
		}
		recordContinuations(web, output);
		assert.equal(compareContinuations(native, [output]), continuationCases.length);
		writeFileSync(
			join(web, continuationCases[0][1]),
			"console.error('synthetic fixture rejected'); process.exitCode = 9;",
		);
		assert.throws(() => recordContinuations(web, output), /continuation failed \(9\): synthetic fixture rejected/);
	} finally {
		rmSync(directory, { recursive: true, force: true });
	}
});

test("cross-platform continuations compare every recorded case for both WASM variants", () => {
	const directory = mkdtempSync(join(tmpdir(), "phoenix-continuations-"));
	try {
		const native = join(directory, "native");
		const ordinary = join(directory, "ordinary");
		const threaded = join(directory, "threaded");
		for (const target of [native, ordinary, threaded]) mkdirSync(target);
		for (const [name] of continuationCases) {
			const trace = JSON.stringify({ case: name, text: "船", quantity: 17 });
			writeFileSync(join(native, name), `\uFEFF${trace}\r\n`);
			writeFileSync(join(ordinary, name), `${trace}\n`);
			writeFileSync(join(threaded, name), trace);
		}
		assert.equal(compareContinuations(native, [ordinary, threaded]), continuationCases.length * 2);
		writeFileSync(join(native, "manual-profile.trace"), "unrelated manual capture");
		assert.equal(compareContinuations(native, [ordinary, threaded]), continuationCases.length * 2);
		const name = continuationCases.at(-1)[0];
		writeFileSync(join(threaded, name), JSON.stringify({ case: name, text: "船", quantity: 18 }));
		assert.throws(
			() => compareContinuations(native, [ordinary, threaded]),
			/interdiction-continuation.trace differs/,
		);
		unlinkSync(join(threaded, name));
		assert.throws(() => compareContinuations(native, [ordinary, threaded]), /Missing or unexpected/);
		writeFileSync(join(threaded, name), readFileSync(join(native, name)));
		writeFileSync(join(native, "unregistered-continuation.trace"), "new native case");
		assert.throws(() => compareContinuations(native, [ordinary, threaded]), /Missing or unexpected/);
		assert.throws(() => compareContinuations(native, []), /At least one/);
	} finally {
		rmSync(directory, { recursive: true, force: true });
	}
});
