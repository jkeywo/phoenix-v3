import { test } from "node:test";
import assert from "node:assert/strict";
import { WebSocket } from "ws";
import { createSignallingServer } from "./server.mjs";
import { readFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawn } from "node:child_process";
const open = (url) =>
	new Promise((resolve, reject) => {
		const socket = new WebSocket(url);
		socket.once("open", () => resolve(socket));
		socket.once("error", reject);
	});

function receiver(socket) {
	const pending = [],
		waiting = [];
	socket.on("message", (bytes) => {
		const packet = JSON.parse(bytes);
		if (waiting.length) waiting.shift()(packet);
		else pending.push(packet);
	});
	return () =>
		pending.length
			? Promise.resolve(pending.shift())
			: new Promise((resolve, reject) => {
					const timer = setTimeout(() => reject(new Error("Envelope timeout")), 2000);
					waiting.push((packet) => {
						clearTimeout(timer);
						resolve(packet);
					});
				});
}
test("isolated discovery, connection-bound routing, opaque relay and duplicate identity refusal", async () => {
	const service = createSignallingServer({
		port: 0,
	});
	const address = await service.listen();
	const url = `ws://127.0.0.1:${address.port}/phoenix`;
	const sockets = [];
	try {
		const a = await open(url);
		sockets.push(a);
		const ra = receiver(a);
		a.send(
			JSON.stringify({
				schema: 1,
				op: "hello",
				room: "synthetic",
				peer: "a",
			}),
		);
		assert.equal((await ra()).op, "ready");
		const b = await open(url);
		sockets.push(b);
		const rb = receiver(b);
		b.send(
			JSON.stringify({
				schema: 1,
				op: "hello",
				room: "synthetic",
				peer: "b",
			}),
		);
		assert.deepEqual((await rb()).peers, ["a"]);
		assert.equal((await ra()).op, "arrived");
		a.send(
			JSON.stringify({
				schema: 1,
				op: "relay",
				from: "forged",
				to: "b",
				data: "opaque-cpp-packet",
			}),
		);
		assert.deepEqual(await rb(), {
			schema: 1,
			op: "relay",
			from: "a",
			data: "opaque-cpp-packet",
		});
		b.send(
			JSON.stringify({
				schema: 1,
				op: "signal",
				to: "a",
				data: "opaque-description",
			}),
		);
		assert.equal((await ra()).op, "signal");
		const other = await open(url);
		sockets.push(other);
		const ro = receiver(other);
		other.send(
			JSON.stringify({
				schema: 1,
				op: "hello",
				room: "other",
				peer: "a",
			}),
		);
		await ro();
		other.send(
			JSON.stringify({
				schema: 1,
				op: "relay",
				to: "b",
				data: "must-not-cross-room",
			}),
		);
		assert.equal((await ro()).op, "unavailable");
		const duplicate = await open(url);
		sockets.push(duplicate);
		const rd = receiver(duplicate);
		duplicate.send(
			JSON.stringify({
				schema: 1,
				op: "hello",
				room: "synthetic",
				peer: "a",
			}),
		);
		assert.equal((await rd()).op, "error");
		b.close();
		assert.deepEqual(await ra(), {
			schema: 1,
			op: "left",
			peer: "b",
		});
	} finally {
		for (const socket of sockets) socket.terminate();
		await service.close();
	}
});
test("public plaintext bind is refused", () => {
	assert.throws(
		() =>
			createSignallingServer({
				host: "0.0.0.0",
			}),
		/requires TLS/,
	);
});

test(
	"live sessions verify content, commit controls, approve GM and explicitly replace disconnected operators",
	{
		skip: !process.env.PHOENIX_SESSION_PROBE,
		timeout: 60000,
	},
	async () => {
		const service = createSignallingServer({
				port: 0,
			}),
			address = await service.listen();
		const url = `ws://127.0.0.1:${address.port}/phoenix`,
			directory = mkdtempSync(join(tmpdir(), "phoenix-session-"));
		const children = [];
		const run = (args, profile) => {
			const child = spawn(
				process.env.PHOENIX_SESSION_PROBE,
				[
					"--url",
					url,
					"--room",
					"live-integration",
					"--data",
					join(directory, profile),
					"--seconds",
					"12",
					...args,
				],
				{
					windowsHide: true,
				},
			);
			children.push(child);
			let output = "";
			child.stdout.on("data", (bytes) => {
				output += bytes;
			});
			child.stderr.on("data", (bytes) => {
				output += bytes;
			});
			const result = new Promise((resolve, reject) => {
				child.once("error", reject);
				child.once("exit", (code) =>
					resolve({
						code,
						get output() {
							return output;
						},
					}),
				);
			});
			return {
				child,
				result,
			};
		};
		let host;
		try {
			host = run(["--host", "--self", "host", "--approve-gm", "--seconds", "55"], "host");
			const first = await run(["--self", "first"], "first").result;
			assert.equal(first.code, 0, first.output);
			assert.match(first.output, /PASS remote verified content/);
			const gm = await run(["--gm", "--self", "gm"], "gm").result;
			assert.equal(gm.code, 0, gm.output);
			assert.match(gm.output, /PASS host-approved read-only GM/);
			// Expiry uses the application's heartbeat timeout, independently of discovery.
			await new Promise((resolve) => setTimeout(resolve, 3200));
			const pending = await run(["--self", "replacement"], "replacement").result;
			assert.equal(pending.code, 0, pending.output);
			assert.match(pending.output, /NEEDS_CONFIRM/);
			const replacement = await run(["--self", "replacement", "--confirm"], "replacement").result;
			assert.equal(replacement.code, 0, replacement.output);
			assert.match(replacement.output, /PASS remote verified content/);
			const previous = await run(["--self", "first", "--blocked"], "first").result;
			assert.equal(previous.code, 0, previous.output);
			assert.match(previous.output, /PASS unclaimed\/previous operator/);
			host.child.kill();
			await host.result;
			host = run(["--host", "--self", "host", "--seconds", "20"], "new-host-epoch");
			const freshEpoch = await run(["--self", "first"], "first").result;
			assert.equal(freshEpoch.code, 0, freshEpoch.output);
			assert.match(freshEpoch.output, /PASS remote verified content/);
		} finally {
			for (const child of children) child.kill();
			if (host) await host.result;
			await service.close();
			rmSync(directory, {
				recursive: true,
				force: true,
				maxRetries: 10,
				retryDelay: 100,
			});
		}
	},
);
test(
	"native verified WSS accepts the test CA and refuses an untrusted certificate",
	{
		skip: !process.env.PHOENIX_NATIVE_PROBE,
	},
	async () => {
		const certificate = process.env.PHOENIX_TEST_CERT,
			key = process.env.PHOENIX_TEST_KEY;
		const service = createSignallingServer({
			port: 0,
			tls: {
				cert: readFileSync(certificate),
				key: readFileSync(key),
			},
		});
		const address = await service.listen(),
			url = `wss://127.0.0.1:${address.port}/phoenix`;
		const children = [];
		const run = (args) => {
			const child = spawn(process.env.PHOENIX_NATIVE_PROBE, args, {
				windowsHide: true,
			});
			children.push(child);
			let output = "";
			child.stdout.on("data", (bytes) => {
				output += bytes;
			});
			child.stderr.on("data", (bytes) => {
				output += bytes;
			});
			const result = new Promise((resolve, reject) => {
				const timeout = setTimeout(() => {
					child.kill();
					reject(new Error("Native TLS probe timeout"));
				}, 12000);
				child.once("error", (error) => {
					clearTimeout(timeout);
					reject(error);
				});
				child.once("exit", (code) => {
					clearTimeout(timeout);
					resolve({
						code,
						get output() {
							return output;
						},
					});
				});
			});
			return {
				child,
				result,
			};
		};
		let echo;
		try {
			echo = run(["--echo", "--url", url, "--ca", certificate, "--room", "synthetic-tls", "--seconds", "10"]);
			// Both connections may arrive in either order; discovery handles either case.
			const accepted = await run([
				"--url",
				url,
				"--ca",
				certificate,
				"--room",
				"synthetic-tls",
				"--self",
				"trusted",
				"--remote",
				"native",
				"--relay",
				"--seconds",
				"5",
			]).result;
			assert.equal(accepted.code, 0, accepted.output);
			assert.match(accepted.output, /PASS forced application relay/);
			const refused = await run([
				"--url",
				url,
				"--room",
				"synthetic-tls",
				"--self",
				"untrusted",
				"--remote",
				"native",
				"--relay",
				"--seconds",
				"2",
			]).result;
			assert.notEqual(refused.code, 0, "Untrusted WSS certificate must not be accepted");
			assert.match(refused.output, /TLS connection failed/);
		} finally {
			for (const child of children) child.kill();
			if (echo) await echo.result;
			await service.close();
		}
	},
);
