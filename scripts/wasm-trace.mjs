import { closeSync, mkdtempSync, openSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";

// Emscripten prints each line with one writeSync call. Linux pipes can accept
// only part of a large JSON trace, so give the executable a regular file.
const directory = mkdtempSync(join(tmpdir(), "phoenix-wasm-trace-"));
try {
	const path = join(directory, "stdout");
	const descriptor = openSync(path, "w");
	let result;
	try {
		result = spawnSync(process.execPath, process.argv.slice(2), {
			stdio: ["inherit", descriptor, "inherit"],
		});
	} finally {
		closeSync(descriptor);
	}
	if (result.error) throw result.error;
	// writeFileSync retries partial writes, preserving all bytes on the parent pipe.
	writeFileSync(1, readFileSync(path));
	process.exitCode = result.status ?? 1;
} finally {
	rmSync(directory, { recursive: true, force: true });
}
