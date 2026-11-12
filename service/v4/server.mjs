import http from "node:http";
import { createReadStream, statSync, existsSync, realpathSync } from "node:fs";
import { resolve, extname, sep } from "node:path";
import { pathToFileURL } from "node:url";
import { WebSocketServer, WebSocket } from "ws";
import { createHmac, timingSafeEqual, randomBytes, randomInt } from "node:crypto";
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
	const secret = process.env.PHOENIX_IDENTITY_SECRET || token || randomBytes(32).toString("hex");
	const sign = (claims) => {
		const body = Buffer.from(JSON.stringify(claims)).toString("base64url");
		return body + "." + createHmac("sha256", secret).update(body).digest("base64url");
	};
	const verify = (credential, room, peer, port) => {
		try {
			const [body, sig] = credential.split(".");
			const expected = createHmac("sha256", secret).update(body).digest();
			const actual = Buffer.from(sig, "base64url");
			if (actual.length !== expected.length || !timingSafeEqual(actual, expected)) return false;
			const c = JSON.parse(Buffer.from(body, "base64url"));
			return c.room === room && c.peer === peer && c.expires > Date.now() && (port !== 9001 || c.role === "host");
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
			res.end(JSON.stringify({ ok: true, version: 1, peers: peers.size }));
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
			QRCode.toString(`${origin.replace(/\/$/, "")}/?room=${code}`, { type: "svg", margin: 2 })
				.then((svg) => {
					res.setHeader("Content-Type", "image/svg+xml");
					res.end(svg);
				})
				.catch(() => res.writeHead(500).end());
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
					if (req.url === "/api/sessions") {
						if (token && req.headers.authorization !== `Bearer ${token}`) {
							res.writeHead(401).end();
							return;
						}
						const roster = data.hosts || [1];
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
						const room = randomBytes(12).toString("hex"),
							expires = now + 86400000;
						sessions.set(room, { expires, roster });
						res.setHeader("Content-Type", "application/json");
						res.end(
							JSON.stringify({
								room,
								expires,
								hosts: roster.map((peer) => ({
									peer,
									token: sign({ room, peer, expires, role: "host" }),
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
							peer,
							token: sign({ room: match[1], peer, expires: s.expires, role: "station" }),
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
			else createReadStream(file + ".br").pipe(res);
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
		createReadStream(file, { start, end }).pipe(res);
	});
	const ws = new WebSocketServer({ noServer: true, maxPayload: 96 * 1024, perMessageDeflate: false });
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
				!["packet", "offer", "answer", "candidate"].includes(m.type) ||
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
			target.send(
				JSON.stringify({
					type: m.type,
					from: identity.peer,
					port: identity.port,
					payload: m.payload,
					mid: typeof m.mid === "string" ? m.mid : "",
				}),
			);
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
