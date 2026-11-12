import http from "node:http";
import { createReadStream, statSync, existsSync, realpathSync } from "node:fs";
import { resolve, extname, sep } from "node:path";
import { pathToFileURL } from "node:url";
import { WebSocketServer, WebSocket } from "ws";
import { randomInt, createHmac, timingSafeEqual } from "node:crypto";
import { createIssuer, issueIdentity, readIdentity } from "./identity.mjs";
import QRCode from "qrcode";

export function createService({
	root = resolve("Artifacts/public"),
	token = process.env.PHOENIX_ROOM_TOKEN || "",
	host = "127.0.0.1",
	port = 9084,
	basePath = "",
} = {}) {
	const peers = new Map(),
		sessions = new Map(),
		addresses = new Map();
	const fault = process.env.NODE_ENV === "test" ? JSON.parse(process.env.PHOENIX_TEST_FAULTS || "{}") : {};
	const faultStarted = Date.now();
	let faultSequence = 0,
		pendingFaultPackets = 0;
	const traffic = { receivedBytes: 0, sentBytes: 0, peakBufferedBytes: 0, httpBodyBytes: 0, httpRequests: 0 };
	const serveFile = (file, options, response) => {
		traffic.httpRequests++;
		const stream = createReadStream(file, options);
		stream.on("data", (chunk) => (traffic.httpBodyBytes += chunk.length));
		stream.pipe(response);
	};
	const faultStats = { delayed: 0, dropped: 0, snapshotInterrupted: 0 };
	const sign = (claims) => {
		const identity = issueIdentity(sessions.get(claims.room), claims);
		return token ? identity + "." + createHmac("sha256", token).update(identity).digest("base64url") : identity;
	};
	const verify = (credential, room, peer, port) => {
		try {
			let identity = credential;
			if (token) {
				const parts = credential.split(".");
				if (parts.length !== 3) return false;
				identity = parts.slice(0, 2).join(".");
				const expected = createHmac("sha256", token).update(identity).digest(),
					actual = Buffer.from(parts[2], "base64url");
				if (actual.length !== expected.length || !timingSafeEqual(actual, expected)) return false;
			}
			const c = readIdentity(identity);
			const valid =
				c.room === room &&
				c.peer === peer &&
				c.expires > Date.now() &&
				(c.role === "host" || (c.role === "station" && port !== 9001 && port !== 7777));
			// The signed host credential carries enough metadata to restore discovery
			// after a service restart. Gameplay and history remain entirely on hosts.
			if (valid && c.role === "host" && Array.isArray(c.roster) && !sessions.has(room) && sessions.size < 1000)
				sessions.set(room, {
					expires: c.expires,
					roster: c.roster,
					scenario: c.scenario || "base",
					publicKey: c.publicKey,
					privateKey: c.issuer,
				});
			return valid;
		} catch {
			return false;
		}
	};
	const server = http.createServer((req, res) => {
		res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
		res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
		res.setHeader("Cross-Origin-Resource-Policy", "same-origin");
		if (req.url === "/health") {
			res.writeHead(200, { "Content-Type": "application/json" });
			res.end(
				JSON.stringify({
					ok: true,
					version: 1,
					peers: peers.size,
					traffic,
					...(process.env.NODE_ENV === "test" ? { faultStats } : {}),
				}),
			);
			return;
		}
		if (req.url?.startsWith("/qr?")) {
			const code = new URL(req.url, "http://localhost").searchParams.get("code");
			if (!/^[a-f0-9]{24}$/.test(code || "")) {
				res.writeHead(400).end();
				return;
			}
			const origin =
				process.env.PHOENIX_PUBLIC_URL ||
				`${req.headers["x-forwarded-proto"] === "https" ? "https" : "http"}://${req.headers.host}${basePath}`;
			const png = new URL(req.url, "http://localhost").searchParams.get("format") === "png";
			const joinUrl = `${origin.replace(/\/$/, "")}/?room=${code}`;
			const qr = png
				? QRCode.toBuffer(joinUrl, { type: "png", width: 640, margin: 4 })
				: QRCode.toString(joinUrl, { type: "svg", margin: 2 });
			qr.then((data) => {
				res.setHeader("Content-Type", png ? "image/png" : "image/svg+xml");
				res.setHeader("Cache-Control", "no-store");
				res.end(data);
			}).catch(() => res.writeHead(500).end());
			return;
		}
		if (req.method === "POST" && req.url?.startsWith("/api/sessions")) {
			let body = "";
			req.on("data", (chunk) => {
				body += chunk;
				if (body.length > 4096) req.destroy();
			});
			req.on("end", () => {
				try {
					const data = JSON.parse(body || "{}");
					const now = Date.now();
					for (const [id, s] of sessions) if (s.expires < now) sessions.delete(id);
					if (req.url === "/api/sessions" || req.url === "/api/sessions/recover") {
						const recovery = req.url.endsWith("/recover");
						let previous;
						if (recovery) {
							try {
								previous = JSON.parse(Buffer.from(data.credential.split(".")[0], "base64url"));
							} catch {}
							if (
								!previous ||
								previous.role !== "host" ||
								!verify(data.credential, previous.room, previous.peer, 9001)
							) {
								res.writeHead(401).end();
								return;
							}
						}
						if (!recovery && token && req.headers.authorization !== `Bearer ${token}`) {
							res.writeHead(401).end();
							return;
						}
						const roster = recovery ? [previous.peer] : data.hosts || [1];
						const scenario = recovery ? previous.scenario || "base" : data.scenario || "base";
						if (!["base", "example"].includes(scenario)) {
							res.writeHead(400).end();
							return;
						}
						if (
							!Array.isArray(roster) ||
							roster.length < 1 ||
							roster.length > 3 ||
							new Set(roster).size !== roster.length ||
							roster.some((id) => !Number.isInteger(id) || id < 1 || id > 99) ||
							sessions.size >= 1000
						) {
							res.writeHead(400).end();
							return;
						}
						const issuer = createIssuer(),
							room = issuer.room,
							expires = now + 86400000;
						sessions.set(room, { expires, roster, scenario, ...issuer });
						res.setHeader("Content-Type", "application/json");
						res.end(
							JSON.stringify({
								room,
								expires,
								hosts: roster.map((peer) => ({
									peer,
									token: sign({ room, peer, expires, role: "host", roster, scenario }),
								})),
							}),
						);
						return;
					}
					const match = /^\/api\/sessions\/([a-f0-9]{24})\/join$/.exec(req.url);
					const s = match && sessions.get(match[1]);
					if (!s) {
						res.writeHead(404).end();
						return;
					}
					const peer = randomInt(100, 0x7fffffff);
					res.setHeader("Content-Type", "application/json");
					res.end(
						JSON.stringify({
							room: match[1],
							scenario: s.scenario,
							peer,
							token: sign({
								room: match[1],
								peer,
								expires: s.expires,
								role: "station",
							}),
						}),
					);
				} catch {
					res.writeHead(400).end();
				}
			});
			return;
		}
		let pathname;
		try {
			pathname = decodeURIComponent(new URL(req.url, "http://localhost").pathname);
		} catch {
			res.writeHead(400).end();
			return;
		}
		const base = resolve(root),
			file = resolve(base, "." + pathname + (pathname.endsWith("/") ? "index.html" : ""));
		if (!file.startsWith(base + sep) || !existsSync(file)) {
			res.writeHead(404).end();
			return;
		}
		if (!realpathSync(file).startsWith(realpathSync(base) + sep)) {
			res.writeHead(403).end();
			return;
		}
		const stat = statSync(file);
		if (!stat.isFile()) {
			res.writeHead(404).end();
			return;
		}
		const types = {
			".wasm": "application/wasm",
			".js": "text/javascript",
			".mjs": "text/javascript",
			".json": "application/json",
			".html": "text/html",
			".css": "text/css",
			".png": "image/png",
			".svg": "image/svg+xml",
		};
		res.setHeader("Content-Type", types[extname(file)] || "application/octet-stream");
		res.setHeader("Accept-Ranges", "bytes");
		res.setHeader(
			"Cache-Control",
			pathname.includes("/versions/") ? "public,max-age=31536000,immutable" : "no-cache",
		);
		if (!req.headers.range && /\bbr\b/.test(req.headers["accept-encoding"] || "") && existsSync(file + ".br")) {
			const compressed = statSync(file + ".br");
			res.setHeader("Vary", "Accept-Encoding");
			res.setHeader("Content-Encoding", "br");
			res.setHeader("Content-Length", compressed.size);
			res.writeHead(200);
			if (req.method === "HEAD") res.end();
			else serveFile(file + ".br", {}, res);
			return;
		}
		let start = 0,
			end = stat.size - 1,
			status = 200;
		if (req.headers.range) {
			const m = /^bytes=(\d+)-(\d*)$/.exec(req.headers.range);
			if (!m) {
				res.writeHead(416).end();
				return;
			}
			start = Number(m[1]);
			end = m[2] ? Number(m[2]) : end;
			if (start > end || end >= stat.size) {
				res.writeHead(416, { "Content-Range": `bytes */${stat.size}` }).end();
				return;
			}
			status = 206;
			res.setHeader("Content-Range", `bytes ${start}-${end}/${stat.size}`);
		}
		res.setHeader("Content-Length", end - start + 1);
		res.writeHead(status);
		if (req.method === "HEAD") {
			res.end();
			return;
		}
		serveFile(file, { start, end }, res);
	});
	const ws = new WebSocketServer({
		noServer: true,
		maxPayload: 96 * 1024,
		perMessageDeflate: false,
	});
	server.on("upgrade", (req, socket, head) => {
		if (new URL(req.url, "http://localhost").pathname !== "/peer") {
			socket.destroy();
			return;
		}
		ws.handleUpgrade(req, socket, head, (s) => ws.emit("connection", s, req));
	});
	ws.on("connection", (socket, request) => {
		const address = request.socket.remoteAddress || "unknown";
		if (ws.clients.size > 256 || (addresses.get(address) || 0) >= 32) {
			socket.close(1013, "connection limit");
			return;
		}
		addresses.set(address, (addresses.get(address) || 0) + 1);
		let alive = true;
		socket.on("pong", () => (alive = true));
		const heartbeat = setInterval(() => {
			if (!alive) {
				socket.terminate();
				return;
			}
			alive = false;
			socket.ping();
		}, 20000);
		heartbeat.unref();
		let identity = null,
			window = Date.now(),
			count = 0;
		const deadline = setTimeout(() => socket.close(1008, "registration timeout"), 5000);
		socket.on("error", () => {});
		socket.on("message", (raw) => {
			traffic.receivedBytes += raw.length;
			if (Date.now() - window > 1000) {
				window = Date.now();
				count = 0;
			}
			if (++count > 2000) {
				socket.close(1008, "rate limit");
				return;
			}
			let m;
			try {
				m = JSON.parse(raw);
			} catch {
				socket.close(1008, "invalid JSON");
				return;
			}
			if (!identity) {
				const localSecretAllowed = host === "127.0.0.1" && (!token || m.token === token);
				if (
					m.type !== "hello" ||
					typeof m.room !== "string" ||
					!/^[A-Za-z0-9_-]{1,64}$/.test(m.room) ||
					!Number.isSafeInteger(m.peer) ||
					m.peer < 1 ||
					m.peer > 0x7fffffff ||
					!Number.isInteger(m.port) ||
					m.port < 1 ||
					m.port > 65535 ||
					(!localSecretAllowed && !verify(m.token, m.room, m.peer, m.port))
				) {
					socket.close(1008, "invalid registration");
					return;
				}
				const key = `${m.room}/${m.peer}/${m.port}`;
				if (peers.has(key)) {
					socket.close(1008, "identity in use");
					return;
				}
				identity = { room: m.room, peer: m.peer, port: m.port, key };
				peers.set(key, socket);
				clearTimeout(deadline);
				socket.send(JSON.stringify({ type: "ready", peer: m.peer, port: m.port }));
				return;
			}
			if (
				!["packet", "offer", "answer", "candidate", "connect"].includes(m.type) ||
				!Number.isInteger(m.to) ||
				!Number.isInteger(m.port)
			)
				return;
			if (typeof m.payload !== "string" || m.payload.length > 88000) return;
			const target = peers.get(`${identity.room}/${m.to}/${m.port}`);
			if (!target || target.readyState !== WebSocket.OPEN) return;
			if (target.bufferedAmount > 4 * 1024 * 1024) {
				target.close(1013, "backpressure");
				return;
			}
			traffic.peakBufferedBytes = Math.max(traffic.peakBufferedBytes, target.bufferedAmount);
			const packet = JSON.stringify({
				type: m.type,
				from: identity.peer,
				port: identity.port,
				payload: m.payload,
				mid: typeof m.mid === "string" ? m.mid : "",
			});
			traffic.sentBytes += Buffer.byteLength(packet);
			if (m.type === "packet" && process.env.NODE_ENV === "test") {
				const elapsed = Date.now() - faultStarted;
				if (
					fault.partitionPeer &&
					elapsed >= Number(fault.partitionAfterMs) &&
					elapsed < Number(fault.partitionAfterMs) + Number(fault.partitionDurationMs) &&
					(identity.peer === fault.partitionPeer || m.to === fault.partitionPeer)
				) {
					faultStats.dropped++;
					return;
				}
				if (fault.interruptSnapshotTo === m.to && !faultStats.snapshotInterrupted) {
					try {
						const content = JSON.parse(Buffer.from(m.payload, "base64"));
						if (
							content.type === "consensus" &&
							content.body?.type === "snapshot" &&
							content.body.size > 16000
						) {
							target.send(packet);
							faultStats.snapshotInterrupted++;
							target.close(1012, "test checkpoint interruption");
							console.log(
								"PHOENIX_TEST_SNAPSHOT_INTERRUPT",
								m.to,
								content.body.offset,
								content.body.size,
							);
							return;
						}
					} catch {}
				}
				faultSequence = (Math.imul(faultSequence || 12345, 1664525) + 1013904223) >>> 0;
				if (faultSequence % 100 < Math.min(25, Math.max(0, Number(fault.lossPercent) || 0))) {
					faultStats.dropped++;
					return;
				}
				const delay =
					Math.min(1000, Math.max(0, Number(fault.latencyMs) || 0)) +
					(faultSequence % (1 + Math.min(500, Math.max(0, Number(fault.jitterMs) || 0))));
				if (delay) {
					if (pendingFaultPackets >= 1024) {
						faultStats.dropped++;
						return;
					}
					pendingFaultPackets++;
					faultStats.delayed++;
					setTimeout(() => {
						pendingFaultPackets--;
						if (target.readyState === WebSocket.OPEN) target.send(packet);
					}, delay);
					return;
				}
			}
			target.send(packet);
		});
		socket.on("close", () => {
			clearInterval(heartbeat);
			const remaining = (addresses.get(address) || 1) - 1;
			if (remaining) addresses.set(address, remaining);
			else addresses.delete(address);
			clearTimeout(deadline);
			if (identity && peers.get(identity.key) === socket) peers.delete(identity.key);
		});
	});
	return {
		server,
		ws,
		listen: () => new Promise((r) => server.listen(port, host, r)),
		close: () =>
			new Promise((r) => {
				for (const p of ws.clients) p.terminate();
				ws.close(() => server.close(r));
			}),
	};
}
if (import.meta.url === pathToFileURL(process.argv[1] || "").href) {
	const host = process.env.PHOENIX_BIND || "127.0.0.1";
	if (host !== "127.0.0.1" && !process.env.PHOENIX_ROOM_TOKEN)
		throw Error("Public service requires PHOENIX_ROOM_TOKEN");
	const app = createService({
		host,
		port: Number(process.env.PORT || 9084),
		root: process.env.PHOENIX_STATIC_ROOT || resolve("Artifacts/public"),
	});
	await app.listen();
	console.log("Phoenix service listening");
}
