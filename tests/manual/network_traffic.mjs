import { createRequire } from "node:module";
import { createSignallingServer } from "../../service/server.mjs";
const require = createRequire(new URL("../../service/package.json", import.meta.url));
const { WebSocket, WebSocketServer } = require("ws");

export async function createTrafficObserver(dropEvery = 0) {
	const backend = createSignallingServer({ port: 0 });
	const backendAddress = await backend.listen();
	const proxy = new WebSocketServer({ port: 0, host: "127.0.0.1", maxPayload: 262144, perMessageDeflate: false });
	await new Promise((done, fail) => {
		proxy.once("listening", done);
		proxy.once("error", fail);
	});
	let phase = "startup",
		measurementStart = 0,
		measuredPackets = 0,
		dropped = 0;
	const totals = { startup: {}, measured: {}, teardown: {} },
		connections = new Set(),
		seenOffers = new Set();
	const bytes = (value) => (typeof value === "string" ? Buffer.byteLength(value) : value.length);
	function observe(peer, direction, message) {
		const length = bytes(message);
		let type = "signalling",
			application = 0,
			offer;
		try {
			const outer = JSON.parse(message.toString());
			if (outer.op === "relay") {
				application = Buffer.byteLength(outer.data);
				const packet = JSON.parse(outer.data);
				type = packet.type || "opaque";
				if (type === "state-offer") offer = packet.body;
			}
		} catch {
			type = "malformed";
		}
		const bucket = (totals[phase][peer] ??= { outbound: {}, inbound: {} });
		const stream = bucket[direction];
		stream.messages = (stream.messages || 0) + 1;
		stream.relay_message_bytes = (stream.relay_message_bytes || 0) + length;
		stream.application_packet_bytes = (stream.application_packet_bytes || 0) + application;
		// RFC 6455 payload length/header fields; TLS/TCP/IP are excluded explicitly.
		const header = (length < 126 ? 2 : length < 65536 ? 4 : 10) + (direction === "outbound" ? 4 : 0);
		stream.websocket_header_bytes = (stream.websocket_header_bytes || 0) + header;
		stream.by_type ??= {};
		const category = (stream.by_type[type] ??= {
			messages: 0,
			relay_message_bytes: 0,
			application_packet_bytes: 0,
		});
		category.messages++;
		category.relay_message_bytes += length;
		category.application_packet_bytes += application;
		if (offer && Number.isSafeInteger(offer.size) && offer.size > 0) {
			const key = JSON.stringify([phase, peer, direction, offer.stream, offer.sequence]);
			if (!seenOffers.has(key)) {
				seenOffers.add(key);
				const sizes = (stream.offered_snapshots ??= { count: 0, total_bytes: 0, maximum_bytes: 0 });
				sizes.count++;
				sizes.total_bytes += offer.size;
				sizes.maximum_bytes = Math.max(sizes.maximum_bytes, offer.size);
			}
		}
		if (phase === "measured") {
			stream.seconds ??= {};
			const second = Math.floor((performance.now() - measurementStart) / 1000);
			stream.seconds[second] = (stream.seconds[second] || 0) + length + header;
		}
		return type;
	}
	proxy.on("connection", (socket) => {
		connections.add(socket);
		let peer = "unidentified",
			queued = [],
			queuedBytes = 0;
		const remote = new WebSocket(`ws://127.0.0.1:${backendAddress.port}/phoenix`, { perMessageDeflate: false });
		connections.add(remote);
		remote.on("open", () => {
			for (const message of queued) remote.send(message, { binary: false });
			queued = [];
			queuedBytes = 0;
		});
		socket.on("message", (message) => {
			try {
				const p = JSON.parse(message.toString());
				if (p.op === "hello") peer = p.peer;
			} catch {}
			const type = observe(peer, "outbound", message);
			if (phase === "measured" && type.startsWith("state-") && dropEvery && ++measuredPackets % dropEvery === 0) {
				dropped++;
				return;
			}
			if (remote.readyState === WebSocket.OPEN) remote.send(message, { binary: false });
			else if (remote.readyState === WebSocket.CONNECTING && (queuedBytes += message.length) <= 8 * 1024 * 1024)
				queued.push(message);
			else socket.close();
		});
		remote.on("message", (message) => {
			let type = "";
			try {
				const p = JSON.parse(message.toString());
				if (p.op === "relay") type = JSON.parse(p.data).type || "";
			} catch {}
			if (phase === "measured" && type.startsWith("state-") && dropEvery && ++measuredPackets % dropEvery === 0) {
				dropped++;
				return;
			}
			observe(peer, "inbound", message);
			if (socket.readyState === WebSocket.OPEN) socket.send(message, { binary: false });
		});
		socket.on("close", () => {
			connections.delete(socket);
			remote.close();
		});
		remote.on("close", () => {
			connections.delete(remote);
			socket.close();
		});
		socket.on("error", () => remote.close());
		remote.on("error", () => socket.close());
	});

	return {
		url: `ws://127.0.0.1:${proxy.address().port}/phoenix`,
		begin() {
			phase = "measured";
			measurementStart = performance.now();
		},
		stop() {
			const duration = measurementStart ? (performance.now() - measurementStart) / 1000 : undefined;
			phase = "teardown";
			return duration;
		},
		summary(duration) {
			for (const bucket of Object.values(totals.measured))
				for (const stream of Object.values(bucket)) {
					if (!stream.messages || !duration) continue;
					const seconds = Array.from(
						{ length: Math.floor(duration) },
						(_, i) => stream.seconds?.[i] || 0,
					).sort((a, b) => a - b);
					stream.mean_bits_per_second =
						(8 * (stream.relay_message_bytes + stream.websocket_header_bytes)) / duration;
					stream.p95_one_second_bits = seconds.length
						? 8 * seconds[Math.floor((seconds.length - 1) * 0.95)]
						: null;
					stream.maximum_one_second_bits = 8 * Math.max(0, ...Object.values(stream.seconds || {}));
				}
			return { traffic: totals, drop_every_state_packet: dropEvery, deliberately_dropped_packets: dropped };
		},
		async close() {
			for (const socket of connections) socket.terminate();
			await new Promise((done) => proxy.close(done));
			await backend.close();
		},
	};
}
