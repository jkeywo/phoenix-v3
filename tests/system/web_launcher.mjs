import assert from "node:assert/strict";
import test from "node:test";
import { Worker } from "node:worker_threads";
import { fileURLToPath } from "node:url";
import { startSessionPump } from "../../web/session-pump.mjs";
import { acquireSessionProfile } from "../../web/session-profile.mjs";
import { deflateRawSync } from "node:zlib";
import {
	viewportLayout,
	menuMotion,
	canvasPoint,
	launchOptions,
	launchArguments,
	skinProperties,
	menuPreview,
	zipEntry,
	mergeRuntimeLibrary,
	refreshPublishedPack,
	togglePack,
} from "../../web/launcher.mjs";
const route = (query) => launchOptions(new URLSearchParams(query), "https://example.test/phoenix-v3/");
test("concurrent default-profile tabs and duplicated tabs keep separate durable identities", async () => {
	const profile = "test-" + crypto.randomUUID();
	const store = (values = new Map()) => ({
		values,
		getItem: (key) => values.get(key),
		setItem: (key, value) => values.set(key, value),
	});
	const tabs = [store(), store()];
	const acquire = (storage) =>
		acquireSessionProfile(profile, { locks: navigator.locks, storage, randomUUID: () => crypto.randomUUID() });
	const leases = [];
	try {
		const initial = await Promise.all(tabs.map(acquire));
		leases.push(...initial);
		assert.equal(new Set(initial.map((lease) => lease.profile)).size, 2);
		assert.ok(initial.some((lease) => lease.profile === profile));
		const duplicate = await acquire(store(new Map(tabs[0].values)));
		leases.push(duplicate);
		assert.ok(initial.every((lease) => lease.profile !== duplicate.profile));
		const reconnect = initial[1].profile;
		initial[1].release();
		// Wait for the lock manager to complete the release before simulating reload.
		await new Promise((resolve) => setTimeout(resolve, 0));
		const reloaded = await acquire(tabs[1]);
		leases.push(reloaded);
		assert.equal(reloaded.profile, reconnect);
	} finally {
		for (const lease of leases) lease.release();
	}
	await new Promise((resolve) => setTimeout(resolve, 0));
	const restarted = await acquire(store());
	assert.equal(restarted.profile, profile);
	restarted.release();
});
function sessionWorker() {
	// Run the shipped browser worker with Node's message-port adapter.
	const worker = new Worker(
		`
		const { parentPort, workerData } = require("node:worker_threads");
		global.self = { postMessage: value => parentPort.postMessage(value) };
		require(workerData);
		parentPort.on("message", data => self.onmessage({ data }));
	`,
		{ eval: true, workerData: fileURLToPath(new URL("../../web/session-clock.js", import.meta.url)) },
	);
	const adapter = {
		postMessage: (value) => worker.postMessage(value),
		terminate: () => worker.terminate(),
	};
	worker.on("message", (data) => adapter.onmessage?.({ data }));
	return adapter;
}
test("independent session pumps progress without animation frames and stop cleanly", async () => {
	const counts = [0, 0];
	const stops = [];
	try {
		await Promise.all(
			counts.map(
				(_, index) =>
					new Promise((resolve, reject) => {
						const timeout = setTimeout(() => reject(Error("Session pump stopped without rendering")), 5000);
						stops.push(
							startSessionPump(() => {
								if (++counts[index] === 4) {
									clearTimeout(timeout);
									resolve();
								}
							}, sessionWorker),
						);
					}),
			),
		);
	} finally {
		for (const stop of stops) stop();
	}
	assert.ok(counts.every((count) => count >= 4));
	const stopped = [...counts];
	await new Promise((resolve) => setTimeout(resolve, 80));
	assert.deepEqual(counts, stopped);
});
test("session clock does not queue ticks while a page cannot service them", async () => {
	const worker = sessionWorker();
	const messages = [];
	try {
		await new Promise((resolve, reject) => {
			const timeout = setTimeout(() => reject(Error("Clock did not start")), 5000);
			worker.onmessage = ({ data }) => {
				messages.push(data);
				clearTimeout(timeout);
				resolve();
			};
			worker.postMessage("next");
			worker.postMessage("next");
		});
		await new Promise((resolve) => setTimeout(resolve, 100));
		assert.deepEqual(messages, ["tick"]);
	} finally {
		worker.terminate();
	}
});
test("new deployments refresh shipped packs while retaining edits and disabled selections", () => {
	const published = { id: "test-base", hash: "new" };
	const library = { packs: [{ id: "test-base", hash: "old", enabled: false, url: "base.zip" }] };
	assert.equal(refreshPublishedPack(library, published, "old", "base"), false);
	assert.deepEqual(library.packs, [{ ...published, enabled: false, url: "base.zip" }]);
	const edited = { packs: [{ id: "test-base", hash: "authored", enabled: true }] };
	refreshPublishedPack(edited, published, "old", "base");
	assert.equal(edited.packs[0].hash, "authored");
	assert.equal(edited.split_bundled_pending, true);
	// A reload before C++ has merged the definitions must retain the pending work.
	refreshPublishedPack(edited, published, "new", "base");
	assert.equal(edited.split_bundled_pending, true);
	const recovered = mergeRuntimeLibrary({ packs: [{ id: "test-base", hash: "merged", enabled: true }] }, edited);
	refreshPublishedPack(recovered, published, "new", "base");
	assert.equal(recovered.packs[0].hash, "merged");
	assert.equal(recovered.split_bundled_pending, undefined);
	const first = { packs: [] };
	assert.equal(refreshPublishedPack(first, published, undefined, "base"), true);
	assert.equal(first.packs[0].enabled, true);
});
test("pack toggles preserve dependency order, disable dependants and allow every pack off", () => {
	const packs = [
		{ id: "practice", hash: "p", enabled: false },
		{ id: "core", hash: "c", enabled: false },
	];
	const metadata = new Map([
		["p", menuPreview({ schema: 1, id: "practice", dependencies: ["core"], scenarios: [] })],
	]);
	const enabled = togglePack(packs, "p", metadata);
	assert.deepEqual(
		enabled.map((p) => [p.id, p.enabled]),
		[
			["core", true],
			["practice", true],
		],
	);
	const off = togglePack(enabled, "c", metadata);
	assert.ok(off.every((p) => !p.enabled));
	assert.deepEqual(
		togglePack(off, "c", metadata).map((p) => p.enabled),
		[true, false],
	);
	assert.throws(() => togglePack([packs[0]], "p", metadata), /required pack/);
	assert.deepEqual(
		packs.map((p) => p.enabled),
		[false, false],
	);
});
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
	const gm = route("type=server&action=host-gm&scenario=test.map");
	assert.equal(gm.action, "host-gm");
	assert.equal(gm.slot, "");
	assert.equal(gm.ship, "");
	assert.throws(() => route("type=client&action=host-gm&scenario=test.map"));
	assert.throws(() => route("type=server&action=host-gm&scenario=test.map&slot=bridge"));
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
	const source = archive(json);
	const compiled = new Uint8Array(8 + source.length);
	compiled.set(new TextEncoder().encode("PHXDAT1\n"));
	compiled.set(source, 8);
	assert.equal(new TextDecoder().decode(await zipEntry(compiled, "launcher.json")), json);
	assert.equal(await zipEntry(archive(json), "absent", true), null);
	await assert.rejects(zipEntry(archive("x".repeat(5000), 8, 3), "launcher.json"));
	await assert.rejects(zipEntry(archive(json, 0, 1024 * 1024 + 1), "launcher.json"));
	await assert.rejects(zipEntry(new Uint8Array([1, 2, 3]), "launcher.json"));
});

// Physical input must address the same logical control after rotation and scaling.
test("portrait and landscape viewport fitting preserves canvas pointer coordinates", () => {
	for (const [width, height] of [
		[390, 844],
		[844, 390],
		[1920, 1080],
		[320, 568],
	]) {
		const v = viewportLayout(width, height);
		assert.ok(v.width >= v.height);
		assert.ok(Math.abs(v.width * v.scale - (v.rotated ? height : width)) < 0.001);
		assert.ok(Math.abs(v.height * v.scale - (v.rotated ? width : height)) < 0.001);
		const rect = { left: 0, top: 0, right: width, width, height };
		for (const [x, y] of [
			[0, 0],
			[v.width, v.height],
			[v.width / 2, v.height / 2],
		]) {
			const screenX = v.rotated ? width - y * v.scale : x * v.scale;
			const screenY = v.rotated ? x * v.scale : y * v.scale;
			const point = canvasPoint(rect, v.width, v.height, v.rotated, screenX, screenY);
			assert.ok(Math.abs(point[0] - x) < 0.001 && Math.abs(point[1] - y) < 0.001);
		}
	}
});

test("title entrance finishes in three seconds on different viewports", () => {
	for (const [width, height, period] of [
		[1000, 600, 400],
		[4000, 500, 900],
	]) {
		const motion = menuMotion(width, height, period);
		assert.equal(motion.logoDuration, 3);
	}
});
