import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import { createHash } from "node:crypto";
const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");
import { zipEntry } from "../web/launcher.mjs";
const root = path.resolve(process.argv[2] || "build/pages");
const published = new Map(),
	sourceSets = [];
const excluded = new Set([
	"test.zip",
	"test-3d.zip",
	"training.zip",
	"test.zip.menu.json",
	"test-3d.zip.menu.json",
	"training.zip.menu.json",
	"alliance_courier_recreated.glb",
	"asteroid_common_1_lod1.glb",
	"phoenix_space_cubemap.png",
]);
async function inspect(folder) {
	for (const entry of await readdir(folder, { withFileTypes: true })) {
		const file = path.join(folder, entry.name);
		if (excluded.has(entry.name)) throw Error("Development content in deployment: " + file);
		if (entry.isDirectory()) await inspect(file);
		else if (entry.name.endsWith(".zip")) {
			// Inspect contents too: renaming a development root to base.zip must fail.
			const bytes = new Uint8Array(await readFile(file));
			const manifest = JSON.parse(new TextDecoder().decode(await zipEntry(bytes, "manifest.json")));
			if (
				{ "base.zip": "phoenix-base", "test-missions.zip": "phoenix-test-missions" }[entry.name] !==
					manifest.id ||
				manifest.development_only
			)
				throw Error("Archive outside production allowlist: " + file);
			const menu = JSON.parse(await readFile(file + ".menu.json", "utf8"));
			published.set(manifest.id, hash(bytes));
			sourceSets.push(menu.sources);
			if (
				menu.id !== manifest.id ||
				menu.hash !== hash(bytes) ||
				menu.sidecar_version !== 1 ||
				!Array.isArray(menu.clients)
			)
				throw Error("Invalid published menu companion: " + file);
			if (
				manifest.id === "phoenix-base" &&
				(menu.scenarios.length !== 1 || menu.scenarios[0].id !== "base.rescue-under-threat")
			)
				throw Error("Production root must contain only Rescue: " + file);
			if (
				manifest.id === "phoenix-test-missions" &&
				(menu.parent !== "phoenix-base" || menu.scenarios.length !== 7)
			)
				throw Error("Invalid bundled test mission mod: " + file);
			for (const client of menu.clients) {
				if (
					!client.file.startsWith(entry.name + ".clients/") ||
					!/^[a-f0-9]{64}\.dat$/.test(client.file.split("/")[1]) ||
					client.file.split("/").length !== 2
				)
					throw Error("Unsafe ship companion path: " + file);
				const dat = await readFile(path.join(folder, client.file));
				if (
					hash(dat) !== client.hash ||
					JSON.stringify(client.sources) !== JSON.stringify(menu.sources) ||
					dat.subarray(0, 7).toString() !== "PHXDAT1"
				)
					throw Error("Invalid ship companion: " + client.file);
			}
			const archive = Buffer.from(bytes);
			for (const name of excluded)
				if (archive.includes(Buffer.from(name)))
					throw Error("Development asset embedded in deployment archive: " + file + ": " + name);
		} else if (entry.name.endsWith(".js")) {
			const source = await readFile(file, "utf8");
			if (/"filename"\s*:\s*"[^"\n]*\/(?:test|test-3d|training)\.zip"/.test(source))
				throw Error("Development content preloaded by " + file);
		}
	}
}
await inspect(root);
for (const id of ["phoenix-base", "phoenix-test-missions"])
	if (!published.has(id)) throw Error("Missing bundled production pack: " + id);
for (const sources of sourceSets) {
	if (
		!Array.isArray(sources) ||
		sources.length < 1 ||
		sources.length > 32 ||
		sources.some(([id, digest]) => published.get(id) !== digest)
	)
		throw Error("Companion source identities do not match the deployed archives");
}
console.log("Deployment verifies the root, bundled mission mod and companions; development roots are excluded.");
