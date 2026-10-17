import http from "node:http";
import https from "node:https";
import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import { WebSocket, WebSocketServer } from "ws";

// Discovery/signalling/opaque relay only. No game admission, world, roster or votes.
export function createSignallingServer({ host = "127.0.0.1", port = 9080, tls, origins = [] } = {}) {
	if (!tls && !["127.0.0.1", "::1", "localhost"].includes(host))
		throw new Error("Non-loopback signalling requires TLS");
	const server = tls ? https.createServer(tls) : http.createServer();
	const sockets = new WebSocketServer({
		noServer: true,
		maxPayload: 262144,
		perMessageDeflate: false,
	});
	const rooms = new Map();
	const connections = new Set();
	const addresses = new Map();
	const valid = (value) => typeof value === "string" && /^[A-Za-z0-9._-]{1,128}$/.test(value);
	const send = (socket, packet) => {
		if (socket.readyState !== WebSocket.OPEN) return;
		if (socket.bufferedAmount > 4 * 1024 * 1024) {
			socket.close(1008, "Relay backpressure");
			return;
		}
		socket.send(
			JSON.stringify({
				schema: 1,
				...packet,
			}),
		);
	};
	server.on("request", (request, response) => {
		response.writeHead(request.url === "/health" ? 200 : 404, {
			"Content-Type": "text/plain",
			"Cache-Control": "no-store",
		});
		response.end(request.url === "/health" ? "Phoenix V3 signalling\n" : "Not found\n");
	});
	server.on("upgrade", (request, socket, head) => {
		const address = request.socket.remoteAddress || "unknown";
		if (
			request.url !== "/phoenix" ||
			connections.size >= 1024 ||
			(addresses.get(address) || 0) >= 32 ||
			(origins.length && request.headers.origin && !origins.includes(request.headers.origin))
		) {
			socket.write("HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
			socket.destroy();
			return;
		}
		sockets.handleUpgrade(request, socket, head, (ws) => sockets.emit("connection", ws, request));
	});
	sockets.on("connection", (socket, request) => {
		const address = request.socket.remoteAddress || "unknown";
		addresses.set(address, (addresses.get(address) || 0) + 1);
		connections.add(socket);
		let room,
			peer,
			alive = true,
			tokens = 240,
			last = Date.now();
		const reject = (message) => {
			send(socket, {
				op: "error",
				message,
			});
			socket.close(1008, message);
		};
		socket.on("pong", () => {
			alive = true;
		});
		socket.on("error", () => {});
		const helloTimer = setTimeout(() => {
			if (!peer) socket.close(1008, "Hello required");
		}, 10000);
		helloTimer.unref();
		socket.on("message", (bytes, binary) => {
			try {
				const now = Date.now();
				tokens = Math.min(240, tokens + (now - last) * 0.12) - 1;
				last = now;
				if (tokens < 0 || binary) return reject("Unsupported or excessive traffic");
				const packet = JSON.parse(bytes.toString("utf8"));
				if (!packet || packet.schema !== 1) return reject("Protocol version mismatch");
				if (!peer) {
					if (packet.op !== "hello" || !valid(packet.room) || !valid(packet.peer))
						return reject("Valid room and peer identity required");
					const existing = rooms.get(packet.room);
					if (
						(existing && (existing.has(packet.peer) || existing.size >= 32)) ||
						(!existing && rooms.size >= 128)
					)
						return reject("Room full or identity already connected");
					room = packet.room;
					peer = packet.peer;
					const members = existing || new Map();
					rooms.set(room, members);
					send(socket, {
						op: "ready",
						peer,
						peers: [...members.keys()],
					});
					for (const member of members.values())
						send(member, {
							op: "arrived",
							peer,
						});
					members.set(peer, socket);
					clearTimeout(helloTimer);
					return;
				}
				if (
					!["signal", "relay"].includes(packet.op) ||
					!valid(packet.to) ||
					packet.to === peer ||
					typeof packet.data !== "string" ||
					Buffer.byteLength(packet.data) > 65536
				)
					return reject("Invalid routed envelope");
				const target = rooms.get(room)?.get(packet.to);
				if (!target) {
					send(socket, {
						op: "unavailable",
						peer: packet.to,
					});
					return;
				}
				// Source comes from this connection's route identity, never a supplied field.
				send(target, {
					op: packet.op,
					from: peer,
					data: packet.data,
				});
			} catch {
				reject("Malformed envelope");
			}
		});
		const heartbeat = setInterval(() => {
			if (!alive) {
				socket.terminate();
				return;
			}
			alive = false;
			socket.ping();
		}, 20000);
		heartbeat.unref();
		socket.on("close", () => {
			clearTimeout(helloTimer);
			clearInterval(heartbeat);
			connections.delete(socket);
			const count = (addresses.get(address) || 1) - 1;
			if (count) addresses.set(address, count);
			else addresses.delete(address);
			const members = rooms.get(room);
			if (members?.get(peer) === socket) {
				members.delete(peer);
				for (const member of members.values())
					send(member, {
						op: "left",
						peer,
					});
				if (!members.size) rooms.delete(room);
			}
		});
	});
	return {
		server,
		listen: () =>
			new Promise((resolve, reject) => {
				server.once("error", reject);
				server.listen(port, host, () => {
					server.off("error", reject);
					resolve(server.address());
				});
			}),
		close: () =>
			new Promise((resolve) => {
				for (const socket of connections) socket.terminate();
				sockets.close(() => server.close(resolve));
			}),
	};
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
	const tls =
		process.env.PHOENIX_TLS_CERT && process.env.PHOENIX_TLS_KEY
			? {
					cert: readFileSync(process.env.PHOENIX_TLS_CERT),
					key: readFileSync(process.env.PHOENIX_TLS_KEY),
				}
			: undefined;
	const service = createSignallingServer({
		host: process.env.PHOENIX_BIND || "127.0.0.1",
		port: Number(process.env.PHOENIX_PORT || 9080),
		tls,
		origins: (process.env.PHOENIX_ORIGINS || "").split(",").filter(Boolean),
	});
	service
		.listen()
		.then((address) => console.log(`Phoenix V3 signalling listening on ${address.address}:${address.port}`))
		.catch((error) => {
			console.error(error.message);
			process.exitCode = 1;
		});
	process.on("SIGINT", () => service.close().then(() => process.exit(0)));
}
