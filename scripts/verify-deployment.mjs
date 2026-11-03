import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import { createHash } from "node:crypto";
import { inflateRawSync } from "node:zlib";
const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");
import { zipEntry } from "../web/launcher.mjs";
const root = path.resolve(process.argv[2] || "build/pages");
const published = new Map(),
	sourceSets = [];
async function verifyDatImages(dat) {
	const zip = dat.subarray(8);
	let end = zip.length - 22;
	for (; end >= Math.max(0, zip.length - 65557); --end) if (zip.readUInt32LE(end) === 0x06054b50) break;
	if (end < 0) throw Error("Missing DAT directory");
	let at = zip.readUInt32LE(end + 16);
	const count = zip.readUInt16LE(end + 10);
	for (let i = 0; i < count; ++i) {
		if (zip.readUInt32LE(at) !== 0x02014b50) throw Error("Invalid DAT directory");
		const length = zip.readUInt16LE(at + 28);
		const name = zip.subarray(at + 46, at + 46 + length).toString("utf8");
		if (name.endsWith(".png")) {
			// Compiled content entries permit 64 MiB plus their envelope (pack.cpp).
			// The launcher's 8 MiB metadata-preview budget does not apply to images.
			const packed = zip.readUInt32LE(at + 20),
				size = zip.readUInt32LE(at + 24),
				offset = zip.readUInt32LE(at + 42),
				method = zip.readUInt16LE(at + 10);
			if (size > 64 * 1024 * 1024 + 16 || zip.readUInt16LE(at + 8) & 1 || zip.readUInt32LE(offset) !== 0x04034b50)
				throw Error("Unsupported or oversized DAT image: " + name);
			const start = offset + 30 + zip.readUInt16LE(offset + 26) + zip.readUInt16LE(offset + 28);
			if (start + packed > zip.length) throw Error("Truncated DAT image: " + name);
			const payload = zip.subarray(start, start + packed);
			if (method !== 0 && method !== 8) throw Error("Unsupported DAT image compression: " + name);
			const image = method === 8 ? inflateRawSync(payload, { maxOutputLength: Math.max(1, size) }) : payload;
			if (image.length !== size) throw Error("Invalid DAT image size: " + name);
			if (Buffer.from(image.subarray(0, 8)).toString() !== "PHXWEBP1")
				throw Error("DAT image has not been upgraded: " + name);
		}
		at += 46 + length + zip.readUInt16LE(at + 30) + zip.readUInt16LE(at + 32);
	}
}
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
				await verifyDatImages(dat);
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
const capabilities = JSON.parse(await readFile(path.join(root, "runtime-capabilities.json"), "utf8"));
for (const name of ["runtime-environment.mjs", "isolation-worker.js", "host-worker-link.mjs"])
	await readFile(path.join(root, name));
if (capabilities.base_archive !== undefined) {
	if (capabilities.base_archive !== "base.mission.dat") throw Error("Invalid packaged base archive");
	for (const file of [
		"host-worker.js",
		"host-worker-link.mjs",
		"phoenix_browser_host.js",
		"phoenix_browser_host.wasm",
	])
		if (!(await readFile(path.join(root, file))).length) throw Error("Empty host worker artifact: " + file);
	const mission = await readFile(path.join(root, capabilities.base_archive));
	const lobby = await readFile(path.join(root, "base.lobby.dat"));
	const missionManifest = JSON.parse(Buffer.from(await zipEntry(mission, "manifest.json")).toString("utf8"));
	const metadata = JSON.parse(Buffer.from(await zipEntry(lobby, "launcher.json")).toString("utf8"));
	const manifest = JSON.parse(Buffer.from(await zipEntry(lobby, "manifest.json")).toString("utf8"));
	if (
		manifest.format !== "phoenix-lobby-metadata" ||
		manifest.id !== "phoenix-base" ||
		missionManifest.id !== manifest.id ||
		missionManifest.development_only ||
		metadata.id !== manifest.id ||
		metadata.lobby_version !== 1 ||
		metadata.mission_file !== capabilities.base_archive ||
		metadata.hash !== hash(mission) ||
		metadata.scenarios.length !== 1 ||
		metadata.scenarios[0].id !== "base.rescue-under-threat"
	)
		throw Error("Invalid split base binding");
	const sidecar = JSON.parse(await readFile(path.join(root, capabilities.base_archive + ".menu.json"), "utf8"));
	if (sidecar.hash !== metadata.hash || JSON.stringify(sidecar.clients) !== JSON.stringify(metadata.clients))
		throw Error("Invalid split base sidecar");
	for (const client of metadata.clients) {
		if (!/^base\.mission\.dat\.clients\/[a-f0-9]{64}\.dat$/.test(client.file))
			throw Error("Unsafe split base ship companion path");
		const dat = await readFile(path.join(root, client.file));
		if (hash(dat) !== client.hash || JSON.stringify(client.sources) !== JSON.stringify(metadata.sources))
			throw Error("Invalid split base ship companion: " + client.file);
		await verifyDatImages(dat);
	}
	await verifyDatImages(mission);
}
for (const role of [
	"server",
	"client",
	...(capabilities.features?.workshop !== false || capabilities.features?.gm !== false ? ["workshop"] : []),
])
	for (const extension of ["js", "wasm", "data"]) {
		if (!(await readFile(path.join(root, `${role}.${extension}`))).length) throw Error("Empty ordinary runtime");
		if (
			capabilities.threaded_directory &&
			!(await readFile(path.join(root, "threaded", `${role}.${extension}`))).length
		)
			throw Error("Empty worker runtime");
	}
if (capabilities.root_threaded) throw Error("Public deployment must retain an ordinary runtime for compatibility");
await inspect(root);
for (const id of ["phoenix-base", ...(capabilities.features?.test_missions !== false ? ["phoenix-test-missions"] : [])])
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
console.log("Deployment verifies selected production packs and companions; development roots are excluded.");

if (capabilities.features) {
	for (const file of ["phoenix_base_verifier.js", "phoenix_base_verifier.wasm"])
		if (!(await readFile(path.join(root, file))).length) throw Error("Missing content verifier: " + file);
	if (capabilities.features.test_missions === false && published.has("phoenix-test-missions"))
		throw Error("Unbundled Test Missions leaked into deployment");
}
