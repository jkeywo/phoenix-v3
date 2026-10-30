import assert from "node:assert/strict";
import test from "node:test";
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
import { deflateRawSync } from "node:zlib";
import { spawnSync } from "node:child_process";

const hash = (bytes) => createHash("sha256").update(bytes).digest("hex");
function archive(name, data, method = 0, declared = data.length) {
	name = Buffer.from(name);
	const packed = method === 8 ? deflateRawSync(data) : data;
	const local = Buffer.alloc(30),
		central = Buffer.alloc(46),
		end = Buffer.alloc(22);
	local.writeUInt32LE(0x04034b50);
	local.writeUInt16LE(method, 8);
	local.writeUInt16LE(name.length, 26);
	central.writeUInt32LE(0x02014b50);
	central.writeUInt16LE(method, 10);
	central.writeUInt32LE(packed.length, 20);
	central.writeUInt32LE(declared, 24);
	central.writeUInt16LE(name.length, 28);
	end.writeUInt32LE(0x06054b50);
	end.writeUInt16LE(1, 10);
	end.writeUInt32LE(local.length + name.length + packed.length, 16);
	return Buffer.concat([local, name, packed, central, name, end]);
}
test("deployment verifies large compiled images without weakening preview or image bounds", () => {
	const directory = mkdtempSync(join(tmpdir(), "phoenix-deployment-"));
	try {
		const write = (name, bytes) => writeFileSync(join(directory, name), bytes);
		write("runtime-capabilities.json", JSON.stringify({ root_threaded: false, threaded_directory: true }));
		for (const name of ["runtime-environment.mjs", "isolation-worker.js"]) write(name, "// fixture");
		mkdirSync(join(directory, "threaded"));
		for (const role of ["server", "client", "workshop"])
			for (const extension of ["js", "wasm", "data"])
				for (const prefix of ["", "threaded/"]) write(`${prefix}${role}.${extension}`, "fixture");
		const packs = [
			["base.zip", "phoenix-base"],
			["test-missions.zip", "phoenix-test-missions"],
		];
		for (const [file, id] of packs) {
			write(file, archive("manifest.json", Buffer.from(JSON.stringify({ id }))));
			mkdirSync(join(directory, file + ".clients"));
		}
		const sources = packs.map(([file, id]) => [
			id,
			hash(archive("manifest.json", Buffer.from(JSON.stringify({ id })))),
		]);
		function stageImage(bytes, method, declared) {
			const dat = Buffer.concat([
				Buffer.from("PHXDAT1\n"),
				archive("assets/synthetic.png", bytes, method, declared),
			]);
			for (const [file, id] of packs) {
				const client = { file: `${file}.clients/${hash(dat)}.dat`, hash: hash(dat), sources };
				write(client.file, dat);
				write(
					file + ".menu.json",
					JSON.stringify({
						id,
						hash: sources.find(([source]) => source === id)[1],
						sidecar_version: 1,
						parent: id === "phoenix-test-missions" ? "phoenix-base" : undefined,
						scenarios: id === "phoenix-base" ? [{ id: "base.rescue-under-threat" }] : Array(7).fill({}),
						clients: [client],
						sources,
					}),
				);
			}
		}
		const run = () =>
			spawnSync(
				process.execPath,
				[fileURLToPath(new URL("../../scripts/verify-deployment.mjs", import.meta.url)), directory],
				{ encoding: "utf8" },
			);
		const image = Buffer.alloc(2 * 1024 * 1024);
		image.write("PHXWEBP1");
		for (const method of [0, 8]) {
			stageImage(image, method);
			const result = run();
			assert.equal(result.status, 0, result.stderr);
		}
		stageImage(Buffer.from("raw PNG"), 0);
		assert.match(run().stderr, /DAT image has not been upgraded/);
		stageImage(image, 8, 64 * 1024 * 1024 + 17);
		assert.match(run().stderr, /oversized DAT image/);
		stageImage(image, 8, 8);
		assert.notEqual(run().status, 0, "inflation must respect the declared size");
		stageImage(image, 0, image.length + 1);
		assert.match(run().stderr, /Invalid DAT image size/);
	} finally {
		rmSync(directory, { recursive: true, force: true });
	}
});
