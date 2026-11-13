import test from "node:test";
import assert from "node:assert/strict";
import { once } from "node:events";
import { WebSocket } from "ws";
import { createSignallingServer } from "./server.mjs";

async function connect(url, hello) {
	const socket = new WebSocket(url);
	await once(socket, "open");
	const ready = once(socket, "message");
	socket.send(JSON.stringify(hello));
	return { socket, ready: JSON.parse((await ready)[0]) };
}
test("shared ingress preserves V3 and separates signed V4 sessions", { timeout: 10000 }, async () => {
	const app = createSignallingServer({
		host: "0.0.0.0",
		tlsProxy: true,
		port: 0,
	});
	const { port } = await app.listen();
	const url = `http://127.0.0.1:${port}`;
	try {
		assert.equal(await (await fetch(url + "/health")).text(), "Phoenix V3 signalling\n");
		assert.equal((await fetch(url + "/v4", { redirect: "manual" })).headers.get("location"), "/v4/");
		const page = await fetch(url + "/v4/");
		assert.equal(page.status, 200);
		assert.equal(page.headers.get("cross-origin-embedder-policy"), "require-corp");
		assert.match(await page.text(), /Phoenix V4 foundation prototype/);
		const a = await connect(url.replace("http", "ws") + "/phoenix", {
			schema: 1,
			op: "hello",
			room: "same-room",
			peer: "one",
		});
		const b = await connect(url.replace("http", "ws") + "/phoenix", {
			schema: 1,
			op: "hello",
			room: "same-room",
			peer: "two",
		});
		const received = once(b.socket, "message");
		a.socket.send(
			JSON.stringify({
				schema: 1,
				op: "relay",
				to: "two",
				data: "v3 preserved",
			}),
		);
		assert.equal(JSON.parse((await received)[0]).data, "v3 preserved");
		const session = await (
			await fetch(url + "/v4/api/sessions", {
				method: "POST",
				body: JSON.stringify({ hosts: [1, 2] }),
			})
		).json();
		const c = await connect(url.replace("http", "ws") + "/v4/peer", {
			type: "hello",
			room: session.room,
			peer: 1,
			port: 9001,
			token: session.hosts[0].token,
		});
		const d = await connect(url.replace("http", "ws") + "/v4/peer", {
			type: "hello",
			room: session.room,
			peer: 2,
			port: 9001,
			token: session.hosts[1].token,
		});
		assert.equal(c.ready.type, "ready");
		const packet = once(d.socket, "message");
		c.socket.send(JSON.stringify({ type: "packet", to: 2, port: 9001, payload: "YWJj" }));
		assert.equal(JSON.parse((await packet)[0]).from, 1);
		const health = await (await fetch(url + "/v4/health")).json();
		assert.equal(health.peers, 2);
		assert.equal((await fetch(url + "/v4/qr?code=" + session.room)).headers.get("content-type"), "image/svg+xml");
		const png = await fetch(url + "/v4/qr?code=" + session.room + "&format=png");
		assert.equal(png.headers.get("content-type"), "image/png");
		assert.deepEqual([...new Uint8Array(await png.arrayBuffer()).slice(0, 8)], [137, 80, 78, 71, 13, 10, 26, 10]);
		assert.equal((await fetch(url + "/v4/..%2Fpackage.json")).status, 404);
	} finally {
		await app.close();
	}
});

import { mkdtemp, writeFile, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createHash } from "node:crypto";
import { brotliCompressSync } from "node:zlib";
import { reconstructWasm } from "./v4/wasm-reconstruct.mjs";
const sha = (data) => createHash("sha256").update(data).digest("hex");
async function fixture(root, name, base, old, target) {
 const path = join(root, name);
 await writeFile(path + ".recipe.json.br", brotliCompressSync(Buffer.from(JSON.stringify({schema:2,base,baseSha256:sha(old),targetSha256:sha(target),targetBytes:target.length,commands:[[0,target.length,0]]}))));
 await writeFile(path + ".difference.br", brotliCompressSync(Buffer.alloc(0)));
 await writeFile(path + ".extra.br", brotliCompressSync(target));
 return path;
}
test("cold delta install resolves later-sorting bases and rejects dependency cycles", async () => {
 const root = await mkdtemp(join(tmpdir(), "phoenix-delta-"));
 try {
  const first=Buffer.from("base"), second=Buffer.from("middle"), final=Buffer.from("final");
  await writeFile(join(root,"z.wasm.br"),brotliCompressSync(first));
  await fixture(root,"f.wasm","z.wasm",first,second);
  const path=await fixture(root,"e.wasm","f.wasm",second,final);
  assert.equal(await reconstructWasm(path,root),true);
  assert.deepEqual(await readFile(path),final);
  const cycle=await fixture(root,"a.wasm","b.wasm",first,second);
  await fixture(root,"b.wasm","a.wasm",second,first);
  await assert.rejects(reconstructWasm(cycle,root),/Cyclic/);
 } finally { await rm(root,{recursive:true,force:true}); }
});
