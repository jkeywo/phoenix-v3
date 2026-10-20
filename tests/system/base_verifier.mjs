import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Exercise the exact worker WASM export with synthetic archives; no shipped tuning.
const filename = path.resolve(process.argv[2] || "phoenix_workshop_test.js");
globalThis.self = { location: { href: pathToFileURL(filename).href } };
const factory = createRequire(import.meta.url)(filename);
// The production module targets workers, so supply Node's equivalent of a file fetch.
const fetch = globalThis.fetch;
globalThis.fetch = async (url, options) =>
	String(url).startsWith("file:")
		? new Response(await readFile(new URL(url)), { headers: { "Content-Type": "application/wasm" } })
		: fetch(url, options);
const module = await factory();

function zip(entries) {
	const local = [],
		central = [];
	let offset = 0;
	for (const [name, source] of Object.entries(entries)) {
		const data = Buffer.from(source),
			key = Buffer.from(name);
		let crc = 0xffffffff;
		for (const byte of data) {
			crc ^= byte;
			for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
		}
		crc = (crc ^ 0xffffffff) >>> 0;
		const header = Buffer.alloc(30),
			directory = Buffer.alloc(46);
		header.writeUInt32LE(0x04034b50);
		header.writeUInt16LE(20, 4);
		header.writeUInt32LE(crc, 14);
		header.writeUInt32LE(data.length, 18);
		header.writeUInt32LE(data.length, 22);
		header.writeUInt16LE(key.length, 26);
		directory.writeUInt32LE(0x02014b50);
		directory.writeUInt16LE(20, 4);
		directory.writeUInt16LE(20, 6);
		directory.writeUInt32LE(crc, 16);
		directory.writeUInt32LE(data.length, 20);
		directory.writeUInt32LE(data.length, 24);
		directory.writeUInt16LE(key.length, 28);
		directory.writeUInt32LE(offset, 42);
		local.push(header, key, data);
		central.push(directory, key);
		offset += header.length + key.length + data.length;
	}
	const index = Buffer.concat(central),
		end = Buffer.alloc(22),
		count = Object.keys(entries).length;
	end.writeUInt32LE(0x06054b50);
	end.writeUInt16LE(count, 8);
	end.writeUInt16LE(count, 10);
	end.writeUInt32LE(index.length, 12);
	end.writeUInt32LE(offset, 16);
	return Buffer.concat([...local, index, end]);
}
function verify(bytes) {
	const pointer = module._malloc(bytes.length);
	try {
		module.HEAPU8.set(bytes, pointer);
		return JSON.parse(
			module.ccall("phoenix_verify_launch_base", "string", ["number", "number"], [pointer, bytes.length]),
		);
	} finally {
		module._free(pointer);
	}
}
const definitions = JSON.parse(await readFile(new URL("../fixtures/content/definitions.json", import.meta.url)));
const pixel = await readFile(new URL("../fixtures/content/pixel.png", import.meta.url));
const source = {
	"manifest.json": JSON.stringify({ schema: 1, id: "test-core", base: { render_mode: "sprites" } }),
	"definitions.json": JSON.stringify(definitions),
	"assets/pixel.png": pixel,
	"launcher.json": JSON.stringify({ scenarios: [{ id: "forged-preview" }] }),
};
const valid = verify(zip(source));
assert.equal(valid.ok, true);
assert.equal(valid.preview.id, "test-core");
assert.equal(valid.preview.scenarios[0].id, definitions.maps[0].id);
assert.equal(valid.preview.base.interface_style, "current");
const missing = { ...source };
delete missing["assets/pixel.png"];
assert.equal(verify(zip(missing)).ok, false);
const models = structuredClone(definitions);
for (const entity of models.entities) entity.render = { scale: 1, model: { asset: "test-core:assets/bad.glb" } };
assert.equal(
	verify(
		zip({
			...source,
			"manifest.json": JSON.stringify({ schema: 1, id: "test-core", base: { render_mode: "models" } }),
			"definitions.json": JSON.stringify(models),
			"assets/bad.glb": Buffer.from([1, 2, 3]),
		}),
	).ok,
	false,
);
assert.equal(
	verify(zip({ ...source, "manifest.json": JSON.stringify({ schema: 1, id: "child", parent: "test-core" }) })).ok,
	false,
);
const legacy = verify(
	zip({
		...source,
		"manifest.json": JSON.stringify({ schema: 1, id: "phoenix-base" }),
		"definitions.json": JSON.stringify(definitions).replaceAll("test-core:", "phoenix-base:"),
	}),
);
assert.equal(legacy.ok, true);
assert.equal(legacy.preview.base.render_mode, "sprites");
assert.equal(verify(Buffer.from([1, 2, 3])).ok, false);
console.log("PASS production WASM launch preflight rejects invalid roots/assets/models and ignores forged previews");
