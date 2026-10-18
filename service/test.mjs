import { test } from "node:test";
import assert from "node:assert/strict";
import { WebSocket } from "ws";
import { createSignallingServer } from "./server.mjs";
import { readFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { randomBytes } from "node:crypto";
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
	"real fleet processes continue with a majority, pause a minority, renew returning authority and reconstruct durable logs",
	{
		skip: !process.env.PHOENIX_FLEET_PROBE,
		timeout: 75000,
	},
	async () => {
		const service = createSignallingServer({
				port: 0,
			}),
			address = await service.listen();
		const url = `ws://127.0.0.1:${address.port}/phoenix`,
			directory = mkdtempSync(join(tmpdir(), "phoenix-fleet-")),
			key = randomBytes(32).toString("hex");
		const children = [];
		const fixtures = new Map();
		const fixture = async (members, mismatch) => {
			const id = members.join("-") + (mismatch ? "-different" : ""),
				file = join(directory, id + ".zip");
			if (fixtures.has(id)) return file;
			await new Promise((resolve, reject) => {
				const child = spawn(
					process.env.PHOENIX_FLEET_PROBE,
					[
						"--prepare",
						"--members",
						members.join(","),
						"--fixture",
						file,
						...(mismatch ? ["--mismatch"] : []),
					],
					{
						windowsHide: true,
					},
				);
				children.push(child);
				let output = "";
				child.stderr.on("data", (bytes) => {
					output += bytes;
				});
				child.once("error", reject);
				child.once("exit", (code) => {
					try {
						assert.equal(code, 0, output);
						resolve();
					} catch (error) {
						reject(error);
					}
				});
			});
			fixtures.set(id, true);
			return file;
		};
		const group = async (name, members, seconds, extra = {}) => {
			const packs = new Map();
			for (const member of members)
				packs.set(member, await fixture(members, (extra[member] || []).includes("--mismatch")));
			const results = await Promise.all(
				members.map(
					(member) =>
						new Promise((resolve, reject) => {
							const file = join(directory, `${name}-${member}.json`),
								child = spawn(
									process.env.PHOENIX_FLEET_PROBE,
									[
										"--url",
										url,
										"--room",
										name === "restart" ? "partition" : name,
										"--session",
										name === "restart" ? "partition" : name,
										"--self",
										member,
										"--members",
										members.join(","),
										"--key",
										key,
										"--data",
										join(directory, name === "restart" ? "partition" : name),
										"--fixture",
										packs.get(member),
										"--result",
										file,
										"--seconds",
										String(seconds),
										"--relay",
										...(extra[member] || []),
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
							child.once("error", reject);
							child.once("exit", (code) => {
								try {
									assert.equal(code, 0, output);
									resolve(JSON.parse(readFileSync(file, "utf8")));
								} catch (error) {
									reject(error);
								}
							});
						}),
				),
			);
			return results;
		};
		try {
			const partition = await group("partition", ["a", "b", "c"], 17, {
				c: ["--drop", "5000", "--heal", "10000"],
			});
			assert.equal(
				new Set(partition.map((result) => result.hash)).size,
				1,
				"Caught-up replicas must have identical production world state",
			);
			for (const result of partition) {
				assert.equal(result.finished, true);
				assert.ok(result.ticks > 0);
				assert.ok(result.absences > 0);
				assert.ok(result.rejoins > 0);
				assert.ok(result.relay_packets > 0);
			}
			assert.equal(partition[2].pause_checked, true);
			const restarted = await group("restart", ["a", "b", "c"], 6);
			assert.equal(
				new Set(restarted.map((result) => result.hash)).size,
				1,
				"Durably reconstructed worlds must agree",
			);
			for (const result of restarted) {
				assert.equal(result.ticks, partition[0].ticks);
				assert.deepEqual(
					result.motion,
					partition[0].motion,
					"Durable replay preserves motion without executing additional ticks",
				);
				assert.ok(result.rejoins > partition[0].rejoins);
			}
			const two = await group("two", ["a", "b"], 12, {
				b: ["--drop", "4000", "--heal", "9000"],
			});
			assert.equal(two[1].pause_checked, true);
			assert.equal(new Set(two.map((result) => result.hash)).size, 1);
			for (const result of two) assert.equal(result.finished, true);
			const mismatch = await group("mismatch", ["a", "b"], 5, {
				b: ["--mismatch"],
			});
			for (const result of mismatch) {
				assert.equal(result.ticks, 0, "Different initial content cannot pass the unanimous start barrier");
				assert.equal(result.finished, false);
			}
			const existing = group("missing-journal", ["a", "b", "c"], 12, { c: ["--seconds", "4"] });
			await new Promise((resolve) => setTimeout(resolve, 6000));
			const refused = await new Promise((resolve, reject) => {
				const child = spawn(
					process.env.PHOENIX_FLEET_PROBE,
					[
						"--url",
						url,
						"--room",
						"missing-journal",
						"--session",
						"missing-journal",
						"--self",
						"c",
						"--members",
						"a,b,c",
						"--key",
						key,
						"--data",
						join(directory, "lost-journal"),
						"--fixture",
						join(directory, "a-b-c.zip"),
						"--seconds",
						"4",
						"--relay",
					],
					{ windowsHide: true },
				);
				children.push(child);
				let output = "";
				child.stdout.on("data", (bytes) => {
					output += bytes;
				});
				child.stderr.on("data", (bytes) => {
					output += bytes;
				});
				child.once("error", reject);
				child.once("exit", (code) => resolve({ code, output }));
			});
			assert.notEqual(refused.code, 0, refused.output);
			assert.match(refused.output, /original durable peer journal/);
			await existing;
		} finally {
			for (const child of children) child.kill();
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
	"playable fleet routes Consoles and recovers a returning member across checkpoints",
	{ skip: !process.env.PHOENIX_SESSION_PROBE, timeout: 120000 },
	async (context) => {
		const service = createSignallingServer({ port: 0 });
		const address = await service.listen();
		const directory = mkdtempSync(join(tmpdir(), "phoenix-playable-fleet-"));
		let child;
		try {
			child = spawn(
				process.env.PHOENIX_SESSION_PROBE,
				[
					"--fleet-journey",
					"--url",
					`ws://127.0.0.1:${address.port}/phoenix`,
					"--room",
					"playable-fleet",
					"--data",
					directory,
				],
				{ windowsHide: true, signal: context.signal },
			);
			let output = "";
			child.stdout.on("data", (data) => {
				output += data;
			});
			child.stderr.on("data", (data) => {
				output += data;
			});
			const code = await new Promise((resolve, reject) => {
				child.once("error", reject);
				child.once("exit", resolve);
			});
			assert.equal(code, 0, output);
			assert.match(output, /PASS playable fleet/);
		} finally {
			if (child && child.exitCode === null && child.signalCode === null) {
				child.kill();
				await new Promise((resolve) => child.once("exit", resolve));
			}
			await service.close();
			rmSync(directory, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
		}
	},
);
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
