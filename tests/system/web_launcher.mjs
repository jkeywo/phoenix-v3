import assert from "node:assert/strict";
import test from "node:test";
import { deflateRawSync } from "node:zlib";
import {
	launchOptions,
	launchArguments,
	skinProperties,
	menuPreview,
	zipEntry,
	automaticHostChoice,
	mergeRuntimeLibrary,
} from "../../web/launcher.mjs";
const route = (query) => launchOptions(new URLSearchParams(query), "https://example.test/phoenix-v3/");
test("runtime profile saves preserve unfetched published packs without carrying URLs to replacements", () => {
	const previous = {
		service: "wss://example.test/phoenix",
		profile: "crew",
		packs: [{ id: "test", hash: "first", url: "base.zip" }],
	};
	const current = { player_name: "Navigator", packs: [{ id: "test", hash: "first", enabled: true }] };
	assert.equal(mergeRuntimeLibrary(current, previous).packs[0].url, "base.zip");
	assert.equal(mergeRuntimeLibrary(current, previous).player_name, "Navigator");
	assert.equal(mergeRuntimeLibrary({ packs: [{ id: "test", hash: "edited" }] }, previous).packs[0].url, undefined);
});
test("host selection skips only unambiguous scenario, slot and ship choices", () => {
	const scene = { id: "test.map", slots: [{ id: "test.slot", ships: [{ id: "test.ship" }] }] };
	const other = { ...scene, id: "test.other" };
	assert.deepEqual(automaticHostChoice([scene]), { scenario: scene.id, slot: "test.slot", ship: "test.ship" });
	assert.equal(automaticHostChoice([]), null);
	assert.equal(automaticHostChoice([scene, other]), null);
	assert.deepEqual(automaticHostChoice([scene, other], scene.id), automaticHostChoice([scene]));
	assert.equal(automaticHostChoice([scene], "missing"), null);
	assert.equal(automaticHostChoice([{ ...scene, slots: [] }]), null);
	assert.equal(
		automaticHostChoice([{ ...scene, slots: [...scene.slots, { ...scene.slots[0], id: "second" }] }]),
		null,
	);
	assert.equal(automaticHostChoice([{ ...scene, slots: [{ id: "test.slot", ships: [] }] }]), null);
	assert.equal(
		automaticHostChoice([{ ...scene, slots: [{ id: "test.slot", ships: [{ id: "a" }, { id: "b" }] }] }]),
		null,
	);
});
test("landing page waits; explicit and QR roles select one appropriate runtime", () => {
	assert.equal(route("role=player&code=aBc123").code, "ABC123");
	assert.equal(route("role=gm&code=ABC123").code, "ABC123");
	assert.equal(route(""), null);
	for (const [query, type, role] of [
		["type=client&code=room/ship", "client", "player"],
		["type=workshop&mode=gm&code=room/ship", "workshop", "gm"],
		["role=ship&code=room/host", "server", "ship"],
		["type=workshop", "workshop", "editor"],
	]) {
		const options = route(query);
		assert.equal(options.type, type);
		assert.equal(options.role, role);
		assert.equal(options.action, role === "editor" ? "workshop" : "join");
		assert.equal(options.service, "wss://site--phoenix-v3-coordinator--lzwyxyg44n9n.code.run/phoenix");
	}
	const host = route("type=server&action=host&scenario=test.map&slot=bridge&ship=test.ship");
	const args = launchArguments(host, "https://example.test/phoenix-v3/");
	const pairs = new Map();
	for (let i = 0; i < args.length; i += 2) pairs.set(args[i], args[i + 1]);
	assert.equal(pairs.get("--scenario"), "test.map");
	assert.equal(pairs.get("--slot"), "bridge");
	assert.equal(pairs.get("--ship"), "test.ship");
	assert.equal(pairs.get("--page"), "https://example.test/phoenix-v3/");
	assert.equal(pairs.get("--action"), "host");
});
test("untrusted launch hints cannot choose arbitrary scripts or conflicting roles", () => {
	for (const query of [
		"type=../../evil",
		"role=admin",
		"type=client&role=gm&code=room/ship",
		"role=player&code=bad-code",
		"type=server&action=host",
		"role=player&code=room/host&service=https://example.test",
		"role=player&code=room/host&profile=../outside",
		"type=client&code=room/host&service=wss://user:secret@example.test",
	])
		assert.throws(() => route(query));
});
test("source sidecars drive the same scalable borders in HTML", () => {
	const source = {
		schema_version: 1,
		files: { sprite: "synthetic.png" },
		image_size: [120, 80],
		nine_slice: { top: 10, right: 10, bottom: 20, left: 20 },
	};
	assert.deepEqual(skinProperties(source, 0.5), {
		image: 'url("assets/menu/synthetic.png")',
		slices: "10 10 20 20",
		edges: "5px 5px 10px 10px",
	});
	assert.throws(() => skinProperties({ ...source, files: { sprite: "../outside.png" } }, 0.5));
	assert.throws(() => skinProperties({ ...source, image_size: [20, 10] }, 0.5));
});
test("untrusted pack previews are bounded display data", () => {
	const preview = {
		schema: 1,
		id: "test-pack",
		scenarios: [{ id: "test.map", slots: [{ id: "test.slot", ships: [{ id: "test.ship" }] }] }],
	};
	assert.equal(menuPreview(preview).scenarios[0].slots[0].ships[0].name, "test.ship");
	assert.throws(() => menuPreview({ ...preview, scenarios: {} }));
	assert.throws(() => menuPreview({ ...preview, id: "x".repeat(257) }));
});
function archive(text, method = 0, declared) {
	const data = Buffer.from(text),
		packed = method === 8 ? deflateRawSync(data) : data,
		name = Buffer.from("launcher.json");
	const local = Buffer.alloc(30);
	local.writeUInt32LE(0x04034b50);
	local.writeUInt16LE(method, 8);
	local.writeUInt16LE(name.length, 26);
	const central = Buffer.alloc(46);
	central.writeUInt32LE(0x02014b50);
	central.writeUInt16LE(method, 10);
	central.writeUInt32LE(packed.length, 20);
	central.writeUInt32LE(declared ?? data.length, 24);
	central.writeUInt16LE(name.length, 28);
	const end = Buffer.alloc(22);
	end.writeUInt32LE(0x06054b50);
	end.writeUInt16LE(1, 10);
	end.writeUInt32LE(local.length + name.length + packed.length, 16);
	return new Uint8Array(Buffer.concat([local, name, packed, central, name, end]));
}
test("pack previews handle stored/deflated ZIPs and bound decompression", async () => {
	const json = JSON.stringify({ schema: 1, scenarios: [] });
	for (const method of [0, 8])
		assert.equal(new TextDecoder().decode(await zipEntry(archive(json, method), "launcher.json")), json);
	assert.equal(await zipEntry(archive(json), "absent", true), null);
	await assert.rejects(zipEntry(archive("x".repeat(5000), 8, 3), "launcher.json"));
	await assert.rejects(zipEntry(archive(json, 0, 1024 * 1024 + 1), "launcher.json"));
	await assert.rejects(zipEntry(new Uint8Array([1, 2, 3]), "launcher.json"));
});
