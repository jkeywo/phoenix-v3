import { readFileSync, writeFileSync, mkdirSync, readdirSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { resolve, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

// These are the same synthetic continuations emitted by check.ps1's native gate.
export const continuationCases = [
	["continuation.trace", "phoenix_simulation_check.js"],
	["combat-continuation.trace", "phoenix_combat_tests.js"],
	["controller-continuation.trace", "phoenix_controller_runtime_tests.js"],
	["conversation-continuation.trace", "phoenix_conversation_tests.js"],
	["psychic-continuation.trace", "phoenix_psychic_tests.js"],
	["sensor-continuation.trace", "phoenix_sensors_tests.js"],
	["medical-continuation.trace", "phoenix_medical_tests.js"],
	...[
		"logistics",
		"docking",
		"cargo",
		"processors",
		"store_effects",
		"directed",
		"abilities",
		"rules",
		"custom_properties",
		"processes",
		"security",
		"away",
		"craft",
		"ftl",
		"missions",
		"interdictions",
	].map((name) => [
		`${name === "missions" ? "mission" : name === "interdictions" ? "interdiction" : name}-continuation.trace`,
		`phoenix_${name}_tests.js`,
	]),
];

export function recordContinuations(webDirectory, outputDirectory) {
	webDirectory = resolve(webDirectory);
	outputDirectory = resolve(outputDirectory);
	mkdirSync(outputDirectory, { recursive: true });
	const capture = fileURLToPath(new URL("./wasm-trace.mjs", import.meta.url));
	for (const [trace, executable] of continuationCases) {
		const result = spawnSync(process.execPath, [capture, join(webDirectory, executable), "--trace"], {
			cwd: webDirectory,
			encoding: "utf8",
			maxBuffer: 64 * 1024 * 1024,
		});
		if (result.error) throw result.error;
		if (result.status !== 0)
			throw new Error(`${executable} continuation failed (${result.status ?? result.signal}): ${result.stderr}`);
		writeFileSync(join(outputDirectory, trace), result.stdout);
		console.log(`Recorded ${trace}`);
	}
}

function readTrace(directory, name) {
	return readFileSync(join(directory, name), "utf8")
		.replace(/^\uFEFF/, "")
		.replace(/\r\n/g, "\n")
		.trim();
}

export function compareContinuations(nativeDirectory, webDirectories) {
	if (!webDirectories.length) throw new Error("At least one WASM continuation directory is required");
	const expected = continuationCases.map(([name]) => name).sort();
	for (const directory of [nativeDirectory, ...webDirectories]) {
		const actual = readdirSync(directory)
			.filter((name) => name === "continuation.trace" || name.endsWith("-continuation.trace"))
			.sort();
		if (JSON.stringify(actual) !== JSON.stringify(expected))
			throw new Error(`Missing or unexpected continuation files in ${directory}`);
	}
	for (const directory of webDirectories)
		for (const [trace] of continuationCases) {
			if (readTrace(nativeDirectory, trace) !== readTrace(directory, trace))
				throw new Error(`Native/WASM ${trace} differs in ${directory}`);
		}
	return continuationCases.length * webDirectories.length;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
	try {
		const [operation, directory, ...others] = process.argv.slice(2);
		if (operation === "record" && directory && others.length === 1) recordContinuations(directory, others[0]);
		else if (operation === "compare" && directory && others.length)
			console.log(`PASS ${compareContinuations(directory, others)} native/WASM continuation comparisons`);
		else
			throw new Error(
				"Usage: wasm-continuations.mjs record web-directory output-directory | compare native-directory wasm-directory [...]",
			);
	} catch (error) {
		console.error(error.message);
		process.exitCode = 1;
	}
}
