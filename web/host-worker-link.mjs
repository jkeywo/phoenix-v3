// The page uses the existing WebSocket/WebRTC adapter. The worker owns the
// protocol state and accounts for sends until the page accepts them.
export function connectHostWorkerLinks(module, worker) {
	const links = new Map();
	const encoder = new TextEncoder();
	let nextNotice = 0;
	const operation = (id, op, peer = "", data = "") => {
		const bytes = encoder.encode(data + "\0");
		const pointer = module._malloc(bytes.length);
		if (!pointer) return 0;
		try {
			module.HEAPU8.set(bytes, pointer);
			return module.ccall(
				"phoenix_worker_link_operation",
				"number",
				["number", "string", "string", "number"],
				[id, op, peer, pointer],
			);
		} finally {
			module._free(pointer);
		}
	};
	const monitor = setInterval(() => {
		for (const [id, link] of links)
			for (const peer of link.routes) {
				const buffered = operation(id, "buffered", peer);
				worker.postMessage({ type: "link-buffered", id, peer, buffered });
				if (buffered < 0) link.routes.delete(peer);
			}
	}, 100);
	const lose = (id, link, peer) => {
		operation(id, "reset", peer);
		link.routes.delete(peer);
		worker.postMessage({ type: "link-notice", id, kind: link.config.events.lost, peer, data: "" });
	};
	module.phoenixWorkerLinkNotice = (id, kind, peer, data) => {
		if (id < 0x80000000 && id >= 0) return false;
		id &= 0x7fffffff;
		const link = links.get(id);
		if (!link) return true;
		const { config, pending } = link;
		const packet = kind === config.events.packet;
		let token = 0;
		if (packet || kind === config.events.ws_packet) {
			const bytes = encoder.encode(data).length;
			if (pending.size >= config.queue_limit || link.queued + bytes > config.queue_bytes) {
				if (packet) lose(id, link, peer);
				else operation(id, "retry");
				return true;
			}
			token = ++nextNotice;
			pending.set(token, bytes);
			link.queued += bytes;
		}
		worker.postMessage({ type: "link-notice", id, kind, peer, data, token });
		return true;
	};
	function receive(message) {
		if (message.type === "link-notice-ack") {
			const link = links.get(message.id);
			if (link?.pending.has(message.token)) {
				link.queued -= link.pending.get(message.token);
				link.pending.delete(message.token);
			}
			return true;
		}
		if (message.type !== "link-operation") return false;
		const { id, op, peer, data, sequence } = message;
		if (op === "open")
			links.set(id, { config: JSON.parse(data), routes: new Set(), pending: new Map(), queued: 0 });
		const link = links.get(id);
		if (!link) return true;
		const result = operation(id, op, peer, data);
		if (op === "close") links.delete(id);
		if (op === "direct" && result) {
			link.routes.add(peer);
		} else if (op === "direct" && !result) {
			lose(id, link, peer);
		} else if (op === "envelope" && !result) {
			operation(id, "retry");
		}
		if (sequence)
			worker.postMessage({
				type: "link-ack",
				id,
				sequence,
				accepted: !!result,
				peer,
				buffered: op === "direct" ? operation(id, "buffered", peer) : 0,
			});
		return true;
	}
	return {
		receive,
		close() {
			clearInterval(monitor);
			for (const id of links.keys()) operation(id, "close");
			links.clear();
			delete module.phoenixWorkerLinkNotice;
		},
	};
}
