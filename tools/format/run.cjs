const fs = require("node:fs");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, "../..");
process.chdir(root);
const check = process.argv.includes("--check");

function run(command, args, options = {}) {
	const result = spawnSync(command, args, { cwd: root, encoding: "utf8", stdio: "inherit", ...options });
	if (result.error) throw result.error;
	if (result.status !== 0) throw new Error(`${command} failed (${result.status}).\n${result.stderr || ""}`);
	return result.stdout;
}

function update(file, source, formatted) {
	if (source.replace(/\r\n/g, "\n") === formatted) return false;
	if (check) {
		console.error(`Needs formatting: ${file}`);
		return true;
	}
	if (fs.readFileSync(file, "utf8") !== source)
		throw new Error(`File changed while formatting: ${file}; retry when edits finish.`);
	fs.writeFileSync(file, formatted);
	return false;
}

async function main() {
	const pythonBin = path.join(root, ".tools/format/python/Scripts");
	if (
		!fs.existsSync(path.join(pythonBin, "clang-format.exe")) ||
		!fs.existsSync(path.join(__dirname, "node_modules/prettier")) ||
		!fs.existsSync(path.join(root, ".tools/format/modules/PSScriptAnalyzer/1.25.0/PSScriptAnalyzer.psd1"))
	)
		throw new Error(
			"Run ./scripts/bootstrap-format.ps1 first. Python 3.10+, Node 22+ and PowerShell 7 are required.",
		);
	const prettier = require("prettier");
	const candidates = run("git", ["ls-files", "-z", "--cached", "--others", "--exclude-standard"], { stdio: "pipe" })
		.split("\0")
		.filter(Boolean);
	// Allow only authored roots; never walk build output, historical snapshots or dependencies.
	const files = [...new Set(candidates)]
		.filter(
			(file) =>
				/^(?:[^/]+|(?:\.github|cmake|content|docs|include|scripts|service|src|tests|web|tools\/format)\/.*)$/.test(
					file,
				) &&
				!/(^|\/)(references|build|\.tools|\.cache|out|target|dist|node_modules|vendor|third_party|generated)(\/|$)/.test(
					file,
				) &&
				!/(^|\/)(package-lock\.json|[^/]+\.min\.(?:js|css))$/.test(file) &&
				fs.existsSync(file) &&
				fs.lstatSync(file).isFile(),
		)
		.sort();
	const cpp = files.filter((file) => /\.(?:c|cc|cpp|cppm|cxx|h|hh|hpp|hxx)$/.test(file));
	const cmake = files.filter((file) => /(^|\/)CMakeLists\.txt$|\.cmake$/.test(file));
	const powershell = files.filter((file) => /\.ps(?:1|d1|m1)$/.test(file));
	let drift = false;
	// Write existing files directly: atomic replacement can fail for open editor files on Windows.
	for (const file of [...cpp, ...cmake]) {
		const source = fs.readFileSync(file, "utf8");
		const isCpp = cpp.includes(file);
		const formatted = run(
			path.join(pythonBin, isCpp ? "clang-format.exe" : "gersemi.exe"),
			isCpp ? ["--Werror", "--style=file", file] : ["--config", ".gersemirc", file],
			{ stdio: "pipe", maxBuffer: 16 * 1024 * 1024 },
		).replace(/\r\n/g, "\n");
		drift = update(file, source, formatted) || drift;
	}
	if (powershell.length)
		run("pwsh", ["-NoProfile", "-File", "tools/format/powershell.ps1", ...(check ? ["-Check"] : [])], {
			input: JSON.stringify(powershell),
			stdio: ["pipe", "inherit", "inherit"],
		});
	let textCount = 0;
	for (const file of files) {
		let parser;
		if (/\.(?:js|cjs|mjs)$/.test(file)) parser = "babel";
		else if (/\.html$/.test(file)) parser = "html";
		else if (/\.css$/.test(file)) parser = "css";
		else if (/\.json$/.test(file)) parser = "json";
		else if (/\.ya?ml$/.test(file) || [".clang-format", ".gersemirc"].includes(file)) parser = "yaml";
		else if (/\.md$/.test(file)) parser = "markdown";
		else continue;
		const source = fs.readFileSync(file, "utf8");
		const formatted = await prettier.format(source, {
			...(await prettier.resolveConfig(file)),
			filepath: file,
			parser,
		});
		textCount++;
		drift = update(file, source, formatted) || drift;
	}
	if (drift) throw new Error("Formatting drift detected.");
	console.log(
		`${check ? "Checked" : "Formatted"} ${cpp.length} C/C++, ${cmake.length} CMake, ${powershell.length} PowerShell and ${textCount} web/data/document files.`,
	);
}
main().catch((error) => {
	console.error(error.message);
	process.exitCode = 1;
});
