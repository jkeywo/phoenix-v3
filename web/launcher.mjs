// The HTML shell gathers launch hints. C++ verifies content, setup and admission.
// Keep one landscape coordinate space for rendering, touch and semantic controls.
export function viewportLayout(width, height, runtime = false) {
	const rotated = height > width;
	const w = rotated ? height : width,
		h = rotated ? width : height;
	const scale = Math.min(1, w / (runtime ? 1100 : 960), h / (runtime ? 720 : 560));
	return { rotated, width: w / scale, height: h / scale, scale };
}
export function canvasPoint(rect, width, height, rotated, clientX, clientY) {
	return rotated
		? [((clientY - rect.top) * width) / rect.height, ((rect.right - clientX) * height) / rect.width]
		: [((clientX - rect.left) * width) / rect.width, ((clientY - rect.top) * height) / rect.height];
}
export function menuMotion(width) {
	return { logoWidth: Math.min(300, Math.max(180, width - 740)), logoDuration: 3 };
}
if (typeof document !== "undefined") {
	const fit = () => {
		const v = viewportLayout(window.innerWidth, window.innerHeight, document.body.classList.contains("running"));
		document.documentElement.dataset.rotated = String(v.rotated);
		document.body.classList.toggle("compact", v.scale < 1);
		Object.assign(document.body.style, {
			width: `${v.width}px`,
			height: `${v.height}px`,
			transform: v.rotated
				? `translateX(${window.innerWidth}px) rotate(90deg) scale(${v.scale})`
				: `scale(${v.scale})`,
		});
		const popup = document.getElementById("popup"),
			popupWidth = Math.min(768, v.width - 32);
		Object.assign(popup.style, {
			width: `${popupWidth}px`,
			maxWidth: "none",
			maxHeight: `${v.height * 0.92}px`,
			transform: `${document.body.style.transform} translate(${(v.width - popupWidth) / 2}px, ${v.height * 0.04}px)`,
		});
		document.documentElement.style.setProperty("--stage-width", `${v.width}px`);
		document.documentElement.style.setProperty("--stage-height", `${v.height}px`);
		const motion = menuMotion(
			v.width,
			v.height,
			Number(getComputedStyle(document.documentElement).getPropertyValue("--pan-seconds")) || 720,
		);
		document.documentElement.style.setProperty("--logo-width", `${motion.logoWidth}px`);
		document.documentElement.style.setProperty("--logo-duration", `${motion.logoDuration}s`);
	};
	window.addEventListener("resize", fit);
	fit();
}
const encoder = new TextEncoder(),
	decoder = new TextDecoder("utf-8", { fatal: true });
const defaults = () => ({
	schema: 1,
	volume: 50,
	sfx_volume: 50,
	master_volume: 50,
	language: "en",
	pseudo: false,
	reduced_motion: false,
	dust_motes: true,
	reference_grid: true,
	player_name: "<username>",
	avatar: [],
	packs: [],
	saves: [],
});
const hex = (bytes) => [...new Uint8Array(bytes)].map((x) => x.toString(16).padStart(2, "0")).join("");
export function launchOptions(params, origin = "http://localhost") {
	const text = (key, limit = 256) => {
		const v = params.get(key) || "";
		if (v.length > limit || /[\x00-\x1f]/.test(v)) throw Error("Invalid launch parameter: " + key);
		return v;
	};
	let type = text("type", 16),
		role = text("role", 16),
		mode = text("mode", 16);
	if (!type && !role) return null;
	if (!type) type = role === "ship" ? "server" : role === "gm" ? "workshop" : "client";
	if (!["server", "client", "workshop"].includes(type)) throw Error("Unknown application role.");
	role ||= type === "server" ? "ship" : type === "client" ? "player" : mode === "gm" ? "gm" : "editor";
	if (
		!{ ship: "server", player: "client", gm: "workshop", editor: "workshop" }[role] ||
		{ ship: "server", player: "client", gm: "workshop", editor: "workshop" }[role] !== type
	)
		throw Error("Conflicting application role.");
	mode = role === "gm" ? "gm" : "editor";
	const enteredCode = text("code", 160),
		code = /^[a-z0-9]{6}$/i.test(enteredCode) ? enteredCode.toUpperCase() : enteredCode,
		scenario = text("scenario"),
		slot = text("slot"),
		ship = text("ship"),
		save = text("save", 64);
	const action =
		text("action", 16) ||
		(role === "editor"
			? "workshop"
			: type === "server"
				? scenario
					? "host"
					: params.has("role") && code
						? "join"
						: "legacy"
				: "join");
	if (!["host", "host-gm", "recover", "join", "workshop", "legacy"].includes(action))
		throw Error("Unknown launch action.");
	if (
		((action === "host" || action === "host-gm" || action === "recover" || action === "legacy") &&
			type !== "server") ||
		(action === "workshop" && role !== "editor") ||
		(action === "join" && role === "editor")
	)
		throw Error("Conflicting launch action.");
	if (action === "join" && !/^(?:[A-Z0-9]{6}|[\w.-]+\/[\w.-]+)$/.test(code))
		throw Error("Enter the host's six-character Join Code.");
	if (action === "host" && (!scenario || !slot || !ship)) throw Error("Choose a scenario and ship first.");
	if (action === "host-gm" && (!scenario || slot || ship))
		throw Error("Choose a scenario without a ship slot for GM hosting.");
	if (action === "recover" && !/^[a-f0-9]{32,64}$/.test(save)) throw Error("Invalid saved game.");
	const service = text("service", 2048) || "wss://site--phoenix-v3-coordinator--lzwyxyg44n9n.code.run/phoenix";
	const url = new URL(service);
	if (!["ws:", "wss:"].includes(url.protocol) || url.username || url.password || url.hash)
		throw Error("Use a ws:// or wss:// network service URL.");
	const profile = text("profile", 64) || "default";
	if (!/^[\w.-]+$/.test(profile))
		throw Error("Use letters, numbers, dots, underscores or hyphens in the local profile.");
	return {
		type,
		role,
		mode,
		action,
		code,
		scenario,
		slot,
		ship,
		save,
		service,
		profile,
		language: text("language", 32) || "en",
		relay: text("relay", 1) === "1",
		textprobe: text("textprobe", 1) === "1",
	};
}
export function launchArguments(options, page) {
	const o = options;
	const entries = {
		mode: o.mode,
		code: o.code,
		slot: o.slot,
		service: o.service,
		profile: o.profile,
		page,
		"text-probe": o.textprobe ? "1" : "0",
		language: o.language,
		relay: o.relay ? "1" : "0",
	};
	if (o.action !== "legacy")
		Object.assign(entries, { action: o.action, scenario: o.scenario, ship: o.ship, save: o.save });
	return Object.entries(entries).flatMap(([key, value]) => ["--" + key, value]);
}
export function skinProperties(sidecar, scale) {
	const [width, height] = sidecar.image_size || [],
		s = sidecar.nine_slice;
	if (
		sidecar.schema_version !== 1 ||
		!/^[\w.-]+\.png$/.test(sidecar.files?.sprite) ||
		!s ||
		!Number.isFinite(scale) ||
		scale <= 0 ||
		![width, height, s.top, s.right, s.bottom, s.left].every(Number.isInteger) ||
		Math.min(s.top, s.right, s.bottom, s.left) < 0 ||
		width <= s.left + s.right ||
		height <= s.top + s.bottom
	)
		throw Error("Invalid HUD sidecar.");
	return {
		image: `url("assets/menu/${sidecar.files.sprite}")`,
		slices: [s.top, s.right, s.bottom, s.left].join(" "),
		edges: [s.top, s.right, s.bottom, s.left].map((v) => v * scale + "px").join(" "),
	};
}
export function sceneProperties(scene) {
	if (scene.schema !== 1 || scene.panorama?.repeat !== "mirror-x") throw Error("Invalid menu scene.");
	for (const asset of [
		scene.panorama,
		scene.foreground,
		scene.terminal,
		scene.flash,
		scene.scutter,
		scene.debris,
		scene.logotype,
	]) {
		if (
			!/^[a-z0-9.-]+\.png$/.test(asset?.file) ||
			!Array.isArray(asset.size) ||
			asset.size.length !== 2 ||
			!asset.size.every((v) => Number.isInteger(v) && v > 0 && v <= 16384)
		)
			throw Error("Invalid menu scene image.");
	}
	const { period_seconds: period, initial_phase: phase } = scene.panorama;
	const range = (value) =>
		Array.isArray(value) &&
		value.length === 2 &&
		value.every((v) => Number.isFinite(v) && v > 0) &&
		value[1] >= value[0];
	const tv = scene.flicker,
		robot = scene.scutter;
	if (
		!range(tv?.interval_seconds) ||
		!Number.isFinite(tv.duration_seconds) ||
		tv.duration_seconds <= 0 ||
		tv.duration_seconds >= tv.interval_seconds[0] ||
		!range(robot.interval_seconds) ||
		!range(robot.fast_crossing_seconds) ||
		!range(robot.slow_crossing_seconds) ||
		robot.fast_crossing_seconds[1] >= robot.slow_crossing_seconds[0] ||
		robot.slow_crossing_seconds[1] >= robot.interval_seconds[0]
	)
		throw Error("Invalid ambient menu metadata.");
	const f = scene.flash;
	if (
		![
			...f.interval_seconds,
			f.ramp_seconds,
			f.short_hold_seconds,
			f.gap_seconds,
			f.long_hold_seconds,
			f.intensity,
		].every((v) => Number.isFinite(v) && v > 0) ||
		f.interval_seconds.length !== 2 ||
		f.interval_seconds[1] < f.interval_seconds[0] ||
		f.intensity > 1 ||
		f.interval_seconds[0] <= 4 * f.ramp_seconds + f.short_hold_seconds + f.gap_seconds + f.long_hold_seconds ||
		f.size.some((v, i) => v !== scene.panorama.size[i])
	)
		throw Error("Invalid menu flash metadata.");
	const [width, height] = scene.terminal.size,
		screen = scene.terminal.screen;
	if (
		!Number.isFinite(period) ||
		period <= 0 ||
		!Number.isFinite(phase) ||
		phase < 0 ||
		phase >= 1 ||
		!screen ||
		![screen.x, screen.y, screen.width, screen.height].every(Number.isFinite) ||
		screen.x < 0 ||
		screen.y < 0 ||
		screen.width <= 0 ||
		screen.height <= 0 ||
		screen.x + screen.width > width ||
		screen.y + screen.height > height
	)
		throw Error("Invalid menu scene geometry.");
	return {
		"--menu-space": `url("assets/menu/${scene.panorama.file}")`,
		"--menu-flash-mask": `url("assets/menu/${f.file}")`,
		"--menu-frame": `url("assets/menu/${scene.foreground.file}")`,
		"--menu-crt": `url("assets/menu/${scene.terminal.file}?v=weathered-2")`,
		"--menu-scutter": `url("assets/menu/${robot.file}")`,
		"--pan-period": period + "s",
		"--pan-seconds": String(period),
		"--debris-period": period / 1.5 + "s",
		"--menu-debris": `url("assets/menu/${scene.debris.file}?v=small-scatter-1")`,
		"--menu-logotype": `url("assets/menu/${scene.logotype.file}?v=trail-1")`,
		"--pan-start": String(-2 * phase),
		"--pan-end": String(-2 * phase - 2),
		"--crt-aspect": width + " / " + height,
		"--crt-display-width": (scene.terminal.display_width || 264) + "px",
		"--crt-left": (100 * screen.x) / width + "%",
		"--crt-top": (100 * screen.y) / height + "%",
		"--crt-width": (100 * screen.width) / width + "%",
		"--crt-height": (100 * screen.height) / height + "%",
	};
}
export function nebulaEnvelope(age, flash) {
	const r = flash.ramp_seconds;
	const pulse = (time, hold) => (time < 0 ? 0 : Math.max(0, Math.min(1, time / r, (2 * r + hold - time) / r)));
	return Math.max(
		pulse(age, flash.short_hold_seconds),
		pulse(age - 2 * r - flash.short_hold_seconds - flash.gap_seconds, flash.long_hold_seconds),
	);
}
export function mergeRuntimeLibrary(index, previous) {
	return {
		...index,
		service: previous.service,
		profile: previous.profile,
		packs: index.packs.map((pack) => ({ ...previous.packs.find((old) => old.hash === pack.hash), ...pack })),
	};
}
// Refresh known shipped archives without replacing a Workshop-authored pack.
export function refreshPublishedPack(library, meta, previousHash, bundle) {
	const existing = library.packs.find((pack) => pack.id === meta.id);
	if (!existing) {
		library.packs.push({ hash: meta.hash, id: meta.id, enabled: library.packs.length === 0, url: `${bundle}.zip` });
		return true;
	}
	if (existing.hash !== meta.hash) {
		if (existing.hash === previousHash || existing.url === `${bundle}.zip`) {
			existing.hash = meta.hash;
			existing.url = `${bundle}.zip`;
		} else if (bundle === "base" && previousHash !== meta.hash) {
			library.split_bundled_pending = true;
		}
	}
	return false;
}
export function togglePack(selection, hash, metadata) {
	const candidate = selection.map((p) => ({ ...p }));
	const chosen = candidate.find((p) => p.hash === hash);
	if (!chosen) throw Error("Select an installed pack.");
	const dependencies = (p) => metadata.get(p.hash)?.dependencies || [];
	const visiting = new Set(),
		done = new Set();
	function enable(pack) {
		if (visiting.has(pack)) throw Error("Pack dependencies contain a cycle.");
		if (done.has(pack)) return;
		visiting.add(pack);
		for (const id of dependencies(pack)) {
			const dependency = candidate.find((p) => p.id === id);
			if (!dependency) throw Error("Install the required pack: " + id);
			enable(dependency);
		}
		pack.enabled = true;
		visiting.delete(pack);
		done.add(pack);
	}
	if (!chosen.enabled) enable(chosen);
	else {
		chosen.enabled = false;
		let changed = true;
		while (changed) {
			changed = false;
			for (const p of candidate)
				if (p.enabled && dependencies(p).some((id) => !candidate.some((q) => q.id === id && q.enabled))) {
					p.enabled = false;
					changed = true;
				}
		}
	}
	const ordered = [];
	done.clear();
	function append(pack) {
		if (done.has(pack)) return;
		done.add(pack);
		if (pack.enabled)
			for (const id of dependencies(pack)) {
				const dependency = candidate.find((p) => p.id === id && p.enabled);
				if (dependency) append(dependency);
			}
		ordered.push(pack);
	}
	candidate.forEach(append);
	return ordered;
}
export function menuPreview(source) {
	const text = (value) => {
		if (typeof value !== "string" || !value.length || value.length > 256) throw Error("Invalid menu preview text.");
		return value;
	};
	const list = (value, max) => {
		if (!Array.isArray(value) || value.length > max) throw Error("Invalid menu preview list.");
		return value;
	};
	if (source.schema !== 1) throw Error("Unsupported menu preview.");
	return {
		schema: 1,
		id: source.id === undefined ? "" : text(source.id),
		name: source.name === undefined ? "" : text(source.name),
		dependencies: list(source.dependencies || [], 32).map(text),
		scenarios: list(source.scenarios, 1024).map((scene) => ({
			id: text(scene.id),
			name: text(scene.name || scene.id),
			slots: list(scene.slots, 64).map((slot) => ({
				id: text(slot.id),
				default_ship: slot.default_ship === undefined ? "" : text(slot.default_ship),
				ships: list(slot.ships, 256).map((ship) => ({ id: text(ship.id), name: text(ship.name || ship.id) })),
			})),
		})),
	};
}
// Bounded preview extraction only. The production ZIP/content loader verifies it again.
export async function zipEntry(bytes, name, optional = false) {
	if (bytes.length > 32 * 1024 * 1024) throw Error("Archive exceeds 32 MiB.");
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	let end = bytes.length - 22;
	for (; end >= Math.max(0, bytes.length - 65557); --end) if (view.getUint32(end, true) === 0x06054b50) break;
	if (end < 0 || end < bytes.length - 65557) throw Error("Invalid ZIP directory.");
	const count = view.getUint16(end + 10, true);
	if (count > 1024) throw Error("Too many ZIP entries.");
	let at = view.getUint32(end + 16, true);
	for (let i = 0; i < count; ++i) {
		if (view.getUint32(at, true) !== 0x02014b50) throw Error("Invalid ZIP entry.");
		const n = view.getUint16(at + 28, true),
			extra = view.getUint16(at + 30, true),
			comment = view.getUint16(at + 32, true);
		const path = decoder.decode(bytes.subarray(at + 46, at + 46 + n));
		if (path === name) {
			const packed = view.getUint32(at + 20, true),
				size = view.getUint32(at + 24, true),
				offset = view.getUint32(at + 42, true),
				method = view.getUint16(at + 10, true);
			if (
				size > 1024 * 1024 ||
				packed > 32 * 1024 * 1024 ||
				view.getUint16(at + 8, true) & 1 ||
				view.getUint32(offset, true) !== 0x04034b50
			)
				throw Error("Unsupported or oversized preview entry.");
			const start = offset + 30 + view.getUint16(offset + 26, true) + view.getUint16(offset + 28, true);
			if (start + packed > bytes.length) throw Error("Truncated ZIP entry.");
			let result = bytes.slice(start, start + packed);
			if (method === 8) {
				const reader = new Blob([result])
					.stream()
					.pipeThrough(new DecompressionStream("deflate-raw"))
					.getReader();
				const chunks = [];
				let total = 0;
				for (;;) {
					const part = await reader.read();
					if (part.done) break;
					total += part.value.length;
					if (total > size) {
						await reader.cancel();
						throw Error("Preview exceeds declared size.");
					}
					chunks.push(part.value);
				}
				result = new Uint8Array(total);
				let p = 0;
				for (const chunk of chunks) {
					result.set(chunk, p);
					p += chunk.length;
				}
			} else if (method !== 0) throw Error("Unsupported ZIP compression.");
			if (result.length !== size) throw Error("Truncated ZIP preview.");
			return result;
		}
		at += 46 + n + extra + comment;
	}
	if (optional) return null;
	throw Error("Archive has no " + name);
}

async function boot() {
	const $ = (id) => document.getElementById(id);
	const status = (message) => {
		$("status").textContent = message;
	};
	let library = defaults(),
		nebulaFlashes,
		menuAmbient,
		db,
		busy = false,
		started = false,
		failed = false,
		returning = false,
		branch = "root",
		scenario = "",
		popupFocus;
	const previews = new Map(),
		files = new Map();
	const draftFields = new Map();
	let savedLibrary = JSON.stringify(library);
	let translations = {},
		persist = Promise.resolve(),
		lastIndex = "";
	const t = (text) => {
		const value = translations[text] || text;
		return library.pseudo ? `[ ${value} ~~~ ]` : value;
	};
	const report = (error) => {
		console.error(error);
		status(t(error.message || String(error)));
	};
	const run = (fn) => async (event) => {
		try {
			await fn(event);
		} catch (error) {
			report(error);
		}
	};
	const menuAudio = (() => {
		let context, gain, decoded;
		const active = new Set();
		const recording = fetch("assets/audio/ui_click.ogg")
			.then((response) => {
				if (!response.ok) throw Error("Click audio unavailable");
				return response.arrayBuffer();
			})
			.catch(() => null);
		const volume = () => {
			if (gain) gain.gain.value = (library.master_volume * library.sfx_volume) / 10000;
		};
		const click = async () => {
			try {
				const Audio = window.AudioContext || window.webkitAudioContext;
				if (!Audio) return;
				if (!context) {
					context = new Audio();
					gain = context.createGain();
					gain.connect(context.destination);
				}
				// Resume synchronously in the button gesture, before decoding/loading awaits.
				await context.resume();
				volume();
				decoded ||= recording.then((bytes) => (bytes ? context.decodeAudioData(bytes) : null));
				const buffer = await decoded;
				if (!buffer || !library.master_volume || !library.sfx_volume) return;
				if (active.size >= 8) {
					const oldest = active.values().next().value;
					active.delete(oldest);
					oldest.stop();
				}
				const voice = context.createBufferSource();
				voice.buffer = buffer;
				voice.playbackRate.value = 0.94 + Math.random() * 0.12;
				voice.connect(gain);
				active.add(voice);
				voice.onended = () => {
					active.delete(voice);
					voice.disconnect();
				};
				voice.start();
			} catch {
				/* Audio failure must never prevent a menu action. */
			}
		};
		for (const root of [$("menu"), $("popup")])
			root.addEventListener(
				"click",
				(event) => {
					const button = event.target.closest("button");
					if (button && !button.disabled) void click();
				},
				{ capture: true },
			);
		window.addEventListener("pagehide", () => {
			void context?.suspend();
		});
		return { volume };
	})();
	function element(tag, text, parent) {
		const el = document.createElement(tag);
		if (text !== undefined) el.textContent = t(text);
		if (parent) parent.append(el);
		return el;
	}
	function button(parent, text, fn, selected = false) {
		const el = element("button", text, parent);
		el.type = "button";
		el.className = "skin" + (selected ? " selected" : "");
		el.disabled = busy;
		if (selected) el.setAttribute("aria-pressed", "true");
		el.onclick = run(fn);
		return el;
	}
	function request(req) {
		return new Promise((resolve, reject) => {
			req.onsuccess = () => resolve(req.result);
			req.onerror = () => reject(req.error);
		});
	}
	async function write(changes) {
		const tx = db.transaction("files", "readwrite");
		const done = new Promise((resolve, reject) => {
			tx.oncomplete = resolve;
			tx.onabort = () => reject(tx.error || Error("Browser storage failed."));
			tx.onerror = () => {};
		});
		for (const [key, value] of changes)
			if (value === null) tx.objectStore("files").delete(key);
			else tx.objectStore("files").put(value, key);
		await done;
		for (const [key, value] of changes)
			if (value === null) files.delete(key);
			else files.set(key, value);
	}
	async function remember(changes = []) {
		const candidate = JSON.stringify(library);
		try {
			await write([...changes, ["library.json", encoder.encode(candidate)]]);
			savedLibrary = candidate;
			updateProfile();
			document.body.classList.toggle("reduced-motion", !!library.reduced_motion);
			nebulaFlashes?.sync();
			menuAmbient?.sync();
			menuAudio.volume();
		} catch (error) {
			library = JSON.parse(savedLibrary);
			menuAudio.volume();
			throw error;
		}
	}
	async function fetchFile(url, limit = 32 * 1024 * 1024, cache = "default") {
		const response = await fetch(url, { cache });
		if (!response.ok) throw Error("Could not load " + url);
		if (+response.headers.get("content-length") > limit) throw Error("Download exceeds size limit.");
		const reader = response.body.getReader(),
			chunks = [];
		let size = 0;
		for (;;) {
			const { value, done } = await reader.read();
			if (done) break;
			size += value.length;
			if (size > limit) {
				await reader.cancel();
				throw Error("Download exceeds size limit.");
			}
			chunks.push(value);
		}
		const result = new Uint8Array(size);
		let p = 0;
		for (const chunk of chunks) {
			result.set(chunk, p);
			p += chunk.length;
		}
		return result;
	}
	async function preview(bytes) {
		const manifest = JSON.parse(decoder.decode(await zipEntry(bytes, "manifest.json")));
		const declared = await zipEntry(bytes, "launcher.json", true);
		if (declared) return menuPreview({ ...JSON.parse(decoder.decode(declared)), ...manifest });
		// Older packs can expose direct authored slots; inherited setup is resolved in C++.
		const definitions = JSON.parse(decoder.decode(await zipEntry(bytes, "definitions.json")));
		return menuPreview({
			schema: 1,
			id: manifest.id,
			name: manifest.name,
			dependencies: manifest.dependencies,
			scenarios: (definitions.maps || [])
				.filter((map) => Array.isArray(map.ship_slots))
				.map((map) => ({
					id: map.id,
					name: map.name || map.id,
					slots: map.ship_slots.map((slot) => ({
						id: slot.id,
						default_ship: slot.default_ship,
						ships: slot.allowed_ships.map((id) => ({
							id,
							name: definitions.entities?.find((e) => e.id === id)?.name || id,
						})),
					})),
				})),
		});
	}
	function scenarios() {
		const result = new Map();
		for (const pack of library.packs)
			if (pack.enabled) for (const scene of previews.get(pack.hash)?.scenarios || []) result.set(scene.id, scene);
		return [...result.values()];
	}
	function choose(next, id = "") {
		if (next === "scenarios" || next === "ships") {
			const scenes = scenarios();
			if (next === "scenarios" && scenes.length === 1) {
				next = "ships";
				id = scenes[0].id;
			}
		}
		branch = next;
		scenario = id;
		render();
		const last = $("columns").lastElementChild;
		last?.scrollIntoView({
			block: "nearest",
			inline: "nearest",
			behavior:
				library.reduced_motion || matchMedia("(prefers-reduced-motion: reduce)").matches ? "instant" : "smooth",
		});
		last?.querySelector("button, input")?.focus({ preventScroll: true });
	}
	function back() {
		if ($("popup").open) {
			$("popup").close();
			return;
		}
		choose(
			branch === "ships"
				? scenarios().length === 1
					? "host"
					: "scenarios"
				: branch === "scenarios"
					? "host"
					: "root",
			branch === "ships" ? scenario : "",
		);
	}
	function field(parent, name, label, value = "", type = "text") {
		const lab = element("label", label, parent);
		const input = element("input", undefined, parent);
		input.name = name;
		input.id = "launch-" + name;
		lab.htmlFor = input.id;
		input.value = value;
		if (parent.closest("#columns") && draftFields.has(name)) input.value = draftFields.get(name);
		input.type = type;
		input.maxLength = name === "service" ? 2048 : name === "code" ? 160 : 64;
		return input;
	}
	function profileFields(parent) {
		const name = field(parent, "player_name", "Player name", library.player_name || "<username>");
		name.placeholder = "<username>";
		if (["Player", "<username>"].includes(name.value)) name.value = "";
		name.required = true;
		name.autocomplete = "nickname";
		name.onchange = run(async () => {
			const value = name.value.trim();
			if (!value || encoder.encode(value).length > 64 || /[\x00-\x1f\x7f]/.test(value))
				throw Error("Enter a player name of up to 64 bytes without control characters.");
			library.player_name = value;
			await remember();
		});
		const choices = element("div", undefined, parent);
		choices.className = "avatar-choices";
		for (let i = 1; i <= 12; ++i) {
			const choice = button(choices, "Avatar " + i, async () => {
				library.avatar = [...(await fetchFile(`assets/menu/avatar-${i}.png`))];
				await remember();
			});
			choice.className = "avatar-choice";
			choice.setAttribute("aria-label", t("Avatar") + " " + i);
			choice.textContent = "";
			const image = element("img", undefined, choice);
			image.src = `assets/menu/avatar-${i}.png`;
			image.alt = "";
		}
		const avatar = field(parent, "avatar_upload", "Upload avatar", "", "file");
		avatar.accept = "image/png,image/jpeg,image/webp";
		avatar.onchange = run(async () => {
			const file = avatar.files[0];
			if (!file) return;
			if (file.size > 8 * 1024 * 1024) throw Error("Choose an avatar image up to 8 MiB.");
			const image = await createImageBitmap(file);
			const canvas = document.createElement("canvas");
			canvas.width = canvas.height = 48;
			const side = Math.min(image.width, image.height);
			canvas
				.getContext("2d")
				.drawImage(image, (image.width - side) / 2, (image.height - side) / 2, side, side, 0, 0, 48, 48);
			image.close();
			const png = await new Promise((resolve) => canvas.toBlob(resolve, "image/png"));
			if (!png || png.size > 12288) throw Error("The avatar image could not be saved.");
			library.avatar = [...new Uint8Array(await png.arrayBuffer())];
			await remember();
			status(t("Avatar saved."));
		});
		return name;
	}
	let portraitChoices;
	function updateProfile() {
		const name = library.player_name || "<username>",
			avatar = $("profile-avatar");
		$("profile-name").textContent = name;
		$("profile-crt").setAttribute("aria-label", t("Your profile") + ": " + name);
		avatar.alt = t("Player avatar");
		const present = library.avatar?.length > 0 && library.avatar.length <= 12288;
		avatar.hidden = !present;
		$("profile-initial").hidden = present;
		if (present) {
			const saved = library.avatar;
			avatar.src = "data:image/png;base64," + btoa(String.fromCharCode(...saved));
			portraitChoices ||= Promise.all(
				Array.from(
					{ length: 12 },
					async (_, i) =>
						new Uint8Array(await (await fetch(`assets/menu/avatar-${i + 1}.png`)).arrayBuffer()),
				),
			);
			portraitChoices
				.then((choices) => {
					if (library.avatar !== saved) return;
					const index = choices.findIndex(
						(bytes) => bytes.length === saved.length && bytes.every((b, i) => b === saved[i]),
					);
					if (index >= 0) avatar.src = `assets/menu/portrait-${index + 1}.png`;
				})
				.catch(() => {});
		} else avatar.removeAttribute("src");
	}
	function connection(parent, join = true) {
		const form = element("form", undefined, parent);
		if (join) {
			const code = field(form, "code", "Join Code", new URLSearchParams(location.search).get("code") || "");
			code.required = true;
			code.autocomplete = "off";
			code.style.textTransform = "uppercase";
		}
		const advanced = element("details", undefined, form);
		element("summary", "Advanced Connection Settings", advanced);
		field(
			advanced,
			"service",
			"Network service",
			library.service ||
				launchOptions(new URLSearchParams("type=client&code=room/server"), location.origin).service,
			"url",
		);
		field(advanced, "profile", "Local profile", library.profile || "default");
		return form;
	}
	function options(values) {
		const p = new URLSearchParams({
			...values,
			service: library.service || "",
			profile: library.profile || "default",
			language: library.language,
		});
		return launchOptions(p, location.origin);
	}
	function render() {
		updateProfile();
		document.body.classList.toggle("cascading", branch !== "root");
		for (const input of $("columns").querySelectorAll("input[name]")) draftFields.set(input.name, input.value);
		if (branch === "ships" && !scenarios().some((scene) => scene.id === scenario)) {
			branch = "scenarios";
			scenario = "";
		}
		document.body.classList.toggle("reduced-motion", library.reduced_motion);
		nebulaFlashes?.sync();
		menuAmbient?.sync();
		$("settings").querySelector("span").textContent = t("Settings");
		$("packs").textContent = t("Mod Packs");
		$("back").textContent = t("Back");
		$("back").hidden = branch === "root";
		const columns = $("columns");
		columns.replaceChildren();
		columns.className = "columns" + (branch === "root" ? " root" : "");
		const column = (label) => {
			const el = element("section", undefined, columns);
			el.className = "column";
			el.setAttribute("aria-label", t(label));
			return el;
		};
		const main = column("Main menu");
		for (const [name, next] of [
			["Host Game", "host"],
			["Join Game (New Ship)", "ship"],
			["Join Game (As Player)", "player"],
			["Join Game (As GM)", "gm"],
		])
			button(
				main,
				name,
				() => choose(next),
				next === branch || (next === "host" && ["scenarios", "ships"].includes(branch)),
			);
		button(main, "Workshop", () => start(options({ type: "workshop" })));
		button(main, "Exit", () => {
			window.close();
			status(t("You can close this tab."));
		});
		if (["host", "scenarios", "ships"].includes(branch)) {
			const host = column("Host Game");
			button(host, "New Game", () => choose("scenarios"), branch !== "host");
			element("h2", "Saved Games", host);
			if (!library.saves.length) element("p", "No saved games yet.", host);
			for (const save of library.saves)
				button(host, save.name + (save.scenario ? " / " + save.scenario : ""), () =>
					start(options({ type: "server", action: "recover", save: save.id })),
				);
			button(host, "Import Save", () => importFile("save"));
			const form = connection(host, false);
			form.onsubmit = (event) => event.preventDefault();
			form.oninput = () => {
				const data = new FormData(form);
				library.service = data.get("service");
				library.profile = data.get("profile");
			};
			form.onchange = run(() => remember());
			if (branch !== "host") {
				if (scenarios().length !== 1) {
					const maps = column("Scenarios");
					element("h2", "Scenarios", maps);
					if (!scenarios().length) element("p", "Enable mod packs to choose a scenario.", maps);
					for (const scene of scenarios())
						button(maps, scene.name || scene.id, () => choose("ships", scene.id), scene.id === scenario);
				}
				if (branch === "ships") {
					const ships = column("Ships"),
						scene = scenarios().find((s) => s.id === scenario);
					button(ships, "GM", () => start(options({ type: "server", action: "host-gm", scenario })));
					for (const slot of scene?.slots || []) {
						element("h2", slot.id, ships);
						const defaultShip = slot.ships.find((ship) => ship.id === slot.default_ship) || slot.ships[0];
						if (defaultShip)
							button(ships, "Default: " + (defaultShip.name || defaultShip.id), () =>
								start(
									options({
										type: "server",
										action: "host",
										scenario,
										slot: slot.id,
										ship: defaultShip.id,
									}),
								),
							);
						for (const ship of slot.ships)
							button(ships, ship.name || ship.id, () =>
								start(
									options({ type: "server", action: "host", scenario, slot: slot.id, ship: ship.id }),
								),
							);
					}
				}
			}
		} else if (["ship", "player", "gm"].includes(branch)) {
			const join = column("Join Game");
			element("h2", "Join Game", join);
			const form = connection(join);
			const connect = button(form, "Connect", () => {});
			connect.type = "submit";
			form.onsubmit = run(async (event) => {
				event.preventDefault();
				const p = new URLSearchParams(new FormData(form));
				p.set("role", branch);
				p.set("action", "join");
				const o = launchOptions(p, location.origin);
				library.player_name = String(library.player_name || "<username>").trim();
				if (!library.player_name || encoder.encode(library.player_name).length > 64)
					throw Error("Enter a player name of up to 64 bytes.");
				library.service = o.service;
				library.profile = o.profile;
				await start(o);
			});
		}
		columns.lastElementChild?.classList.add("active");
	}
	function dialog(title) {
		const popup = $("popup");
		if (!popup.open) popupFocus = document.activeElement;
		popup.replaceChildren();
		const heading = element("h2", title, popup);
		heading.id = "popup-title";
		if (!popup.open) popup.showModal();
		menuAmbient?.sync();
		return popup;
	}
	let settingsTab = "audio";
	function settings(page = settingsTab, focusId) {
		const previousFocus = document.activeElement?.id;
		settingsTab = page;
		const popup = dialog("Settings"),
			tabs = element("div", undefined, popup);
		tabs.className = "settings-tabs";
		tabs.setAttribute("role", "tablist");
		tabs.setAttribute("aria-label", t("Settings"));
		const choices = [
			["audio", "Audio"],
			["profile", "Profile"],
			["language", "Language"],
			["display", "Display"],
		];
		for (const [id, label] of choices) {
			const tab = button(tabs, label, () => settings(id, "settings-tab-" + id));
			tab.id = "settings-tab-" + id;
			tab.setAttribute("role", "tab");
			tab.setAttribute("aria-selected", String(id === page));
			tab.setAttribute("aria-controls", "settings-content");
			tab.classList.toggle("selected", id === page);
			tab.tabIndex = id === page ? 0 : -1;
			tab.onkeydown = (event) => {
				const at = choices.findIndex((choice) => choice[0] === id),
					n = choices.length;
				const next =
					event.key === "ArrowLeft"
						? (at + n - 1) % n
						: event.key === "ArrowRight"
							? (at + 1) % n
							: event.key === "Home"
								? 0
								: event.key === "End"
									? n - 1
									: -1;
				if (next >= 0) {
					event.preventDefault();
					tabs.children[next].click();
				}
			};
		}
		const panel = element("div", undefined, popup);
		panel.id = "settings-content";
		panel.setAttribute("role", "tabpanel");
		panel.setAttribute("aria-labelledby", "settings-tab-" + page);
		if (page === "audio") {
			for (const [key, label] of [
				["master_volume", "Master volume"],
				["volume", "Music volume"],
				["sfx_volume", "SFX volume"],
			]) {
				const slider = field(panel, key, label, library[key], "range");
				slider.min = "0";
				slider.max = "100";
				slider.step = "1";
				const value = element("output", library[key] + "%", panel);
				value.htmlFor = slider.id;
				slider.oninput = run(async () => {
					library[key] = +slider.value;
					value.textContent = library[key] + "%";
					menuAudio.volume();
					await remember();
				});
			}
		} else if (page === "profile") profileFields(panel);
		else if (page === "language") {
			const language = element("label", "Language", panel),
				select = element("select", undefined, language);
			select.setAttribute("aria-label", t("Language"));
			element("option", "English", select).value = "en";
			button(panel, "Pseudo-localisation: " + (library.pseudo ? "On" : "Off"), async () => {
				library.pseudo = !library.pseudo;
				await remember();
				settings();
			}).id = "settings-pseudo";
		} else if (page === "display") {
			button(panel, "Reduced motion: " + (library.reduced_motion ? "On" : "Off"), async () => {
				library.reduced_motion = !library.reduced_motion;
				await remember();
				settings();
			}).id = "settings-motion";
			for (const [key, label] of [
				["dust_motes", "Dust motes"],
				["reference_grid", "Reference grid"],
			])
				button(panel, label + ": " + (library[key] !== false ? "On" : "Off"), async () => {
					library[key] = library[key] === false;
					await remember();
					settings();
				}).id = "settings-" + key;
			if (library.reduced_motion) element("p", "Reduced motion hides dust motes and the reference grid.", panel);
		}
		button(popup, "Back", () => $("popup").close()).id = "settings-close";
		const restored = document.getElementById(focusId || previousFocus);
		(restored && popup.contains(restored) ? restored : document.getElementById("settings-tab-" + page)).focus();
	}

	function packs() {
		const popup = dialog("Mod Packs");
		element(
			"p",
			"Pack choices are verified by C++ when entering the lobby. Active sessions retain their exact content.",
			popup,
		).className = "help";
		button(popup, "Import ZIP Pack", () => importFile("pack"));
		for (const [index, pack] of library.packs.entries()) {
			const row = element("div", undefined, popup);
			row.className = "pack-row";
			const name = previews.get(pack.hash)?.name || pack.id;
			const label = element("label", undefined, row);
			const toggle = element("input", undefined, label);
			toggle.type = "checkbox";
			toggle.checked = pack.enabled;
			element("span", name, label);
			toggle.onchange = run(async () => {
				library.packs = togglePack(library.packs, pack.hash, previews);
				await remember();
				render();
				packs();
			});
			for (const [symbol, title, shift] of [
				["↑", "Move Up", -1],
				["↓", "Move Down", 1],
			]) {
				const move = button(row, symbol, async () => {
					[library.packs[index], library.packs[index + shift]] = [
						library.packs[index + shift],
						library.packs[index],
					];
					await remember();
					packs();
				});
				move.setAttribute("aria-label", t(title) + ": " + name);
				move.disabled = index + shift < 0 || index + shift >= library.packs.length;
			}
			const remove = button(row, "🗑", async () => {
				library.packs.splice(index, 1);
				await remember([
					[`packs/${pack.hash}.zip`, null],
					[`preview/${pack.hash}.json`, null],
				]);
				render();
				packs();
			});
			remove.setAttribute("aria-label", t("Remove from Library") + ": " + name);
		}

		button(popup, "Back", () => popup.close());
	}
	async function importFile(kind) {
		const input = document.createElement("input");
		input.type = "file";
		input.accept = ".zip,application/zip";
		input.onchange = run(async () => {
			const file = input.files[0];
			if (!file) return;
			if (file.size > 32 * 1024 * 1024) throw Error("Archive exceeds 32 MiB.");
			const bytes = new Uint8Array(await file.arrayBuffer());
			if (kind === "pack") {
				const meta = await preview(bytes),
					hash = hex(await crypto.subtle.digest("SHA-256", bytes));
				if (!library.packs.some((p) => p.hash === hash)) {
					library.packs.push({ hash, id: meta.id, enabled: false });
					previews.set(hash, meta);
					await remember([
						[`packs/${hash}.zip`, bytes],
						[`preview/${hash}.json`, encoder.encode(JSON.stringify(meta))],
					]);
				}
				packs();
			} else {
				const id = hex(crypto.getRandomValues(new Uint8Array(16)));
				library.saves.push({ id, name: file.name, scenario: "", saved_at: new Date().toISOString() });
				await remember([[`saves/${id}.zip`, bytes]]);
				render();
				status(t("Save imported. It will be verified before opening its recovery lobby."));
			}
		});
		input.click();
	}
	async function ensurePacks(all = false) {
		for (const pack of library.packs)
			if ((pack.enabled || all) && !files.has(`packs/${pack.hash}.zip`)) {
				if (!pack.url) throw Error("Installed pack archive is missing: " + pack.id);
				status(t("Downloading content...") + " " + pack.id);
				const bytes = await fetchFile(pack.url, undefined, "no-cache");
				if (hex(await crypto.subtle.digest("SHA-256", bytes)) !== pack.hash)
					throw Error("Published content changed. Reload the menu.");
				await write([[`packs/${pack.hash}.zip`, bytes]]);
			}
	}
	function snapshotRuntime() {
		if (!window.Module?.FS || failed) return persist;
		const fs = window.Module.FS;
		try {
			const raw = fs.readFile("/launcher/menu/library.json", { encoding: "utf8" });
			if (raw === lastIndex) return persist;
			const index = mergeRuntimeLibrary(JSON.parse(raw), library),
				changes = [["library.json", encoder.encode(JSON.stringify(index))]];
			const retained = new Set([
				...index.packs.map((pack) => `packs/${pack.hash}.zip`),
				...index.saves.map((save) => `saves/${save.id}.zip`),
			]);
			for (const key of files.keys())
				if (/^(packs|saves)\//.test(key) && !retained.has(key)) changes.push([key, null]);
			for (const pack of index.packs) {
				const path = `/launcher/menu/packs/${pack.hash}.zip`;
				let exists = false;
				try {
					exists = !!fs.lookupPath(path).node;
				} catch {}
				if (exists) changes.push([`packs/${pack.hash}.zip`, fs.readFile(path)]);
				else if (!pack.url && !files.has(`packs/${pack.hash}.zip`))
					throw Error("Installed pack archive is missing: " + pack.id);
			}
			for (const save of index.saves)
				changes.push([`saves/${save.id}.zip`, fs.readFile(`/launcher/menu/saves/${save.id}.zip`)]);
			lastIndex = raw;
			persist = persist
				.catch(() => {})
				.then(() => write(changes))
				.catch((error) => {
					lastIndex = "";
					report(Error("Browser data could not be saved: " + error.message));
					throw error;
				});
			return persist;
		} catch (error) {
			report(error);
			return Promise.reject(error);
		}
	}
	async function returnMenu() {
		if (returning) return;
		returning = true;
		try {
			await snapshotRuntime();
			location.assign(location.origin + location.pathname);
		} catch {
			returning = false;
		}
	}
	async function start(o, direct = false) {
		if (started || busy) return;
		busy = true;
		render();
		$("settings").disabled = $("packs").disabled = true;
		try {
			const migration = new Map();
			if (
				library.split_bundled_pending &&
				(o.action === "host" || o.action === "host-gm" || o.action === "workshop")
			) {
				await ensurePacks(true);
				for (const bundle of ["base", "training"]) {
					const meta = await fetch(`${bundle}.zip.menu.json`, { cache: "no-cache" }).then((r) => r.json());
					const bytes = await fetchFile(`${bundle}.zip`, undefined, "no-cache");
					if (hex(await crypto.subtle.digest("SHA-256", bytes)) !== meta.hash)
						throw Error("Published content changed. Reload the menu.");
					migration.set(bundle, bytes);
				}
			}
			if (o.action === "host" || o.action === "host-gm" || o.action === "legacy" || o.action === "workshop")
				await ensurePacks(o.action === "workshop");
			await remember();
			// No generated JS, WASM or .data is requested before this point.
			started = true;
			if (!direct) {
				const query = new URLSearchParams(o);
				query.set("relay", o.relay ? "1" : "0");
				query.set("textprobe", o.textprobe ? "1" : "0");
				history.replaceState(null, "", "?" + query);
			}
			if ($("popup").open) $("popup").close();
			$("menu").hidden = true;
			$("runtime").hidden = false;
			document.body.classList.add("running");
			window.dispatchEvent(new Event("resize"));
			nebulaFlashes?.sync();
			menuAmbient?.sync();
			status(t("Loading application..."));
			window.Module = {
				canvas: $("canvas"),
				phoenixCanvasPoint: (x, y) =>
					canvasPoint(
						$("canvas").getBoundingClientRect(),
						$("canvas").width,
						$("canvas").height,
						document.documentElement.dataset.rotated === "true",
						x,
						y,
					),
				arguments: launchArguments(o, location.origin + location.pathname),
				print: console.log,
				printErr: console.error,
				phoenixMigrateBundled: migration.size > 0,
				preRun: [
					() => {
						const fs = window.Module.FS;
						fs.mkdirTree("/launcher/menu/packs");
						fs.mkdirTree("/launcher/menu/saves");
						for (const [bundle, bytes] of migration) fs.writeFile(`/launcher/bundled-${bundle}.zip`, bytes);
						for (const [key, bytes] of files)
							if (key === "library.json" || /^(packs|saves)\/[a-f0-9]{32,64}\.zip$/.test(key))
								fs.writeFile("/launcher/menu/" + key, bytes);
						lastIndex = decoder.decode(files.get("library.json"));
					},
				],
				postRun: [
					() => {
						if (!failed) {
							status("");
							$("canvas").focus();
						}
					},
				],
				onAbort: (reason) => launchError("Startup failed: " + String(reason || "Unknown runtime error")),
				setStatus: (message) => {
					if (!failed && message) status(t("Loading application...") + " " + message);
				},
				phoenixPersistMenu: () => {
					return snapshotRuntime();
				},
				phoenixReturnToMenu: returnMenu,
				phoenixLaunchError: launchError,
			};
			const script = document.createElement("script");
			script.src = o.type + ".js";
			script.onerror = () => launchError("Build files unavailable. Build and serve the web output first.");
			document.body.append(script);
		} catch (error) {
			busy = false;
			started = false;
			document.documentElement.classList.remove("direct-launch");
			$("menu").hidden = false;
			$("runtime").hidden = true;
			document.body.classList.remove("running");
			window.dispatchEvent(new Event("resize"));
			$("settings").disabled = $("packs").disabled = false;
			render();
			throw error;
		}
	}
	function launchError(message) {
		failed = true;
		document.documentElement.classList.remove("direct-launch");
		$("runtime").hidden = true;
		$("menu").hidden = false;
		document.body.classList.remove("running");
		window.dispatchEvent(new Event("resize"));
		status(message);
		$("columns").replaceChildren();
		const failure = element("section", "", $("columns"));
		failure.setAttribute("role", "alert");
		element("h2", "Unable to load game", failure);
		element("p", String(message), failure);
		button($("columns"), "Return to Menu", () => location.assign(location.origin + location.pathname)).disabled =
			false;
	}
	// Install before GLFW. Native HTML and semantic controls retain their keyboard behavior.
	for (const type of ["keydown", "keypress", "keyup"])
		window.addEventListener(
			type,
			(event) => {
				if (
					event.target.closest("#menu, #popup") ||
					["INPUT", "BUTTON", "SELECT", "TEXTAREA"].includes(event.target.tagName)
				) {
					// GLFW uses window capture listeners. Run tab navigation before isolating DOM keys.
					if (type === "keydown" && event.target.getAttribute("role") === "tab")
						event.target.onkeydown?.(event);
					if (type === "keydown" && event.key === "Escape" && event.target.closest("#semantics"))
						window.Module?.ccall("phoenix_semantic_action", null, ["string"], ["settings"]);
					event.stopImmediatePropagation();
					if (type === "keydown" && event.key === "Escape" && !$("menu").hidden) {
						if (!$("popup").open) {
							event.preventDefault();
							back();
						}
					}
				}
			},
			true,
		);
	function flashBackdrop(flash) {
		const preference = matchMedia("(prefers-reduced-motion: reduce)");
		let active = false,
			timer = 0,
			frame = 0,
			random = 0x5eed;
		const level = (value) => document.documentElement.style.setProperty("--nebula-flash", String(value));
		const allowed = () =>
			!library.reduced_motion &&
			!preference.matches &&
			!document.hidden &&
			!$("menu").hidden &&
			!document.documentElement.classList.contains("direct-launch");
		const delay = () => {
			random = (Math.imul(random, 1664525) + 1013904223) >>> 0;
			return (
				(flash.interval_seconds[0] +
					((flash.interval_seconds[1] - flash.interval_seconds[0]) * random) / 4294967296) *
				1000
			);
		};
		const clear = () => {
			clearTimeout(timer);
			cancelAnimationFrame(frame);
			level(0);
		};
		const burst = () => {
			if (!allowed()) {
				sync();
				return;
			}
			timer = setTimeout(burst, delay());
			const started = performance.now();
			const duration =
				4 * flash.ramp_seconds + flash.short_hold_seconds + flash.gap_seconds + flash.long_hold_seconds;
			const tick = (time) => {
				if (!allowed()) {
					sync();
					return;
				}
				const age = (time - started) / 1000;
				level(nebulaEnvelope(age, flash) * flash.intensity);
				if (age < duration) frame = requestAnimationFrame(tick);
				else level(0);
			};
			frame = requestAnimationFrame(tick);
		};
		const sync = () => {
			const next = allowed();
			if (next === active) return;
			active = next;
			clear();
			if (active) timer = setTimeout(burst, delay());
		};
		preference.addEventListener("change", sync);
		document.addEventListener("visibilitychange", sync);
		window.addEventListener("pagehide", clear);
		sync();
		return { sync };
	}
	function ambientMenu(scene) {
		const preference = matchMedia("(prefers-reduced-motion: reduce)");
		const seeds = crypto.getRandomValues(new Uint32Array(2));
		const allowed = () =>
			!library.reduced_motion &&
			!preference.matches &&
			!document.hidden &&
			!$("menu").hidden &&
			!$("popup").open &&
			!document.documentElement.classList.contains("direct-launch");
		function effect(config, seed, begin, draw, restore) {
			let enabled = false,
				timer = 0,
				frame = 0,
				random = seed;
			const unit = () => {
				random = (Math.imul(random, 1664525) + 1013904223) >>> 0;
				return random / 4294967296;
			};
			const sample = (range) => range[0] + (range[1] - range[0]) * unit();
			const stop = () => {
				clearTimeout(timer);
				cancelAnimationFrame(frame);
				restore();
			};
			const start = () => {
				if (!allowed()) {
					sync();
					return;
				}
				timer = setTimeout(start, sample(config.interval_seconds) * 1000);
				const selection = begin(unit, sample),
					started = performance.now();
				const tick = (time) => {
					if (!allowed()) {
						sync();
						return;
					}
					const age = (time - started) / 1000;
					if (age >= selection.duration) {
						restore();
						return;
					}
					draw(age, selection);
					frame = requestAnimationFrame(tick);
				};
				frame = requestAnimationFrame(tick);
			};
			const sync = () => {
				const next = allowed();
				if (next === enabled) return;
				enabled = next;
				stop();
				if (enabled) timer = setTimeout(start, sample(config.interval_seconds) * 1000);
			};
			return {
				sync,
				stop: () => {
					enabled = false;
					stop();
				},
			};
		}
		const opacity = (value) => document.documentElement.style.setProperty("--crt-opacity", String(value));
		const tv = effect(
			scene.flicker,
			seeds[0],
			() => ({ duration: scene.flicker.duration_seconds }),
			(age, event) => {
				const phase = age / event.duration;
				opacity(phase < 0.17 ? 0.22 : phase < 0.35 ? 0.92 : phase < 0.5 ? 0.35 : phase < 0.7 ? 0.75 : 1);
			},
			() => opacity(1),
		);
		const robot = $("menu-scutter");
		const scutter = effect(
			scene.scutter,
			seeds[1],
			(unit, sample) => {
				const rightward = unit() > 0.5,
					fast = unit() > 0.5;
				return {
					rightward,
					fast,
					duration: sample(fast ? scene.scutter.fast_crossing_seconds : scene.scutter.slow_crossing_seconds),
				};
			},
			(age, event) => {
				const region = $("menu").getBoundingClientRect(),
					width = Math.min(192, Math.max(96, region.height * 0.22));
				const height = (width * scene.scutter.size[1]) / scene.scutter.size[0];
				const progress = age / event.duration,
					travel = progress * (region.width + width);
				const bob = Math.sin(age * (event.fast ? 16 : 6)) * 1.2;
				robot.hidden = true;
				Object.assign(robot.style, {
					width: width + "px",
					height: height + "px",
					left: (event.rightward ? travel - width : region.width - travel) + "px",
					top: region.height * 0.99 - height + bob + "px",
					transform: event.rightward ? "none" : "scaleX(-1)",
				});
			},
			() => {
				robot.hidden = true;
			},
		);
		const sync = () => {
			tv.sync();
			scutter.sync();
		};
		preference.addEventListener("change", sync);
		document.addEventListener("visibilitychange", sync);
		window.addEventListener("pageshow", sync);
		window.addEventListener("pagehide", () => {
			tv.stop();
			scutter.stop();
		});
		sync();
		return { sync };
	}
	$("settings").onclick = () => settings();
	$("profile-crt").onclick = () => {
		const popup = dialog("Profile");
		profileFields(popup);
		button(popup, "Back", () => popup.close());
	};
	$("packs").onclick = packs;
	$("back").onclick = back;
	$("popup").onclose = () => {
		render();
		popupFocus?.focus();
	};
	try {
		const opening = indexedDB.open("phoenix-v3-menu", 1);
		opening.onupgradeneeded = () => opening.result.createObjectStore("files");
		db = await request(opening);
		db.onversionchange = () => db.close();
		const store = db.transaction("files").objectStore("files");
		const [keys, values] = await Promise.all([request(store.getAllKeys()), request(store.getAll())]);
		keys.forEach((key, i) => files.set(key, values[i]));
		if (files.has("library.json")) {
			library = { ...defaults(), ...JSON.parse(decoder.decode(files.get("library.json"))) };
			if (library.schema !== 1 || !Array.isArray(library.packs) || !Array.isArray(library.saves))
				throw Error("Invalid menu library. Existing browser data was preserved.");
		}
		for (const key of ["volume", "sfx_volume", "master_volume"])
			if (!Number.isInteger(library[key]) || library[key] < 0 || library[key] > 100)
				throw Error("Invalid audio preferences. Existing browser data was preserved.");
		savedLibrary = JSON.stringify(library);
		let routes;
		try {
			routes = launchOptions(new URLSearchParams(location.search), location.origin);
		} catch (error) {
			document.documentElement.classList.remove("direct-launch");
			report(error);
		}
		if (routes) $("menu").hidden = true;
		const data = await Promise.all([
			fetch("assets/locales.json").then((r) => r.json()),
			...["button-generic-9slice", "helm-background-9slice"].map((name) =>
				fetch(`assets/menu/${name}.sprite.json`).then((r) => r.json()),
			),
			fetch("assets/menu/menu-scene.json?v=parallax-1").then((r) => r.json()),
		]);
		translations = data[0].languages.en;
		for (const [name, value] of Object.entries(sceneProperties(data[3])))
			document.documentElement.style.setProperty(name, value);
		window.dispatchEvent(new Event("resize"));
		nebulaFlashes = flashBackdrop(data[3].flash);
		menuAmbient = ambientMenu(data[3]);
		for (const [index, prefix, scale] of [
			[1, "button", 0.075],
			[2, "panel", 0.12],
		]) {
			const properties = skinProperties(data[index], scale),
				style = document.documentElement.style;
			style.setProperty(`--${prefix}-image`, properties.image);
			style.setProperty(prefix === "button" ? "--slice" : "--panel-slice", properties.slices);
			style.setProperty(`--${prefix}-edge`, properties.edges);
		}
		if (routes && !["host", "legacy", "workshop"].includes(routes.action)) {
			await start(routes, true);
			return;
		}
		if (!library.avatar?.length) {
			library.avatar = [
				...(await fetchFile(
					`assets/menu/avatar-${1 + (crypto.getRandomValues(new Uint32Array(1))[0] % 12)}.png`,
				)),
			];
			await remember();
		}
		for (const pack of library.packs) {
			const cached = files.get(`preview/${pack.hash}.json`);
			if (cached) previews.set(pack.hash, menuPreview(JSON.parse(decoder.decode(cached))));
			else if (files.has(`packs/${pack.hash}.zip`)) {
				try {
					previews.set(pack.hash, await preview(files.get(`packs/${pack.hash}.zip`)));
				} catch {}
			}
		}
		const published = new Map();
		let addedTraining = false;
		for (const bundle of ["base", "training"]) {
			const marker = `published-${bundle}-split-catalog.json`;
			const previousHash = files.has(marker) ? decoder.decode(files.get(marker)) : undefined;
			try {
				// IndexedDB markers are fallback data, not proof that the deployment is unchanged.
				const response = await fetch(`${bundle}.zip.menu.json`, { cache: "no-cache" });
				if (!response.ok) throw Error("Base game content unavailable. Import a pack in Mod Packs.");
				const meta = await response.json();
				const parsed = menuPreview(meta);
				if (!/^[a-f0-9]{64}$/.test(meta.hash)) throw Error("Invalid published pack identity.");
				published.set(bundle, parsed);
				const added = refreshPublishedPack(library, meta, previousHash, bundle);
				if (bundle === "training") addedTraining = added;
				previews.set(meta.hash, parsed);
				await remember([
					[`preview/${meta.hash}.json`, encoder.encode(JSON.stringify(meta))],
					[marker, encoder.encode(meta.hash)],
				]);
			} catch (error) {
				const cached = previousHash && files.get(`preview/${previousHash}.json`);
				if (cached) published.set(bundle, menuPreview(JSON.parse(decoder.decode(cached))));
				status(t(error.message));
			}
		}
		const base = library.packs.find((p) => p.id === "phoenix-base");
		const training = library.packs.find((p) => p.id === "phoenix-training");
		const basePreview = base && previews.get(base.hash),
			trainingPreview = published.get("training");
		if (
			basePreview &&
			trainingPreview &&
			(basePreview.scenarios.some((s) => trainingPreview.scenarios.some((t) => t.id === s.id)) ||
				published.get("base")?.scenarios.some((s) => !basePreview.scenarios.some((old) => old.id === s.id)))
		) {
			const maps = new Map(
				basePreview.scenarios
					.filter((s) => !trainingPreview.scenarios.some((t) => t.id === s.id))
					.map((s) => [s.id, s]),
			);
			for (const scene of published.get("base")?.scenarios || [])
				if (!maps.has(scene.id)) maps.set(scene.id, scene);
			const migratedPreview = { ...basePreview, scenarios: [...maps.values()] };
			previews.set(base.hash, migratedPreview);
			library.split_bundled_pending = true;
			if (addedTraining) training.enabled = base.enabled;
			await remember([[`preview/${base.hash}.json`, encoder.encode(JSON.stringify(migratedPreview))]]);
		}
		if (routes) {
			await start(routes, true);
			return;
		}
		render();
		if (!$("status").textContent || $("status").textContent === "Loading menu…") status("");
	} catch (error) {
		document.documentElement.classList.remove("direct-launch");
		$("menu").hidden = false;
		report(error);
		render();
	}
}
if (typeof document !== "undefined") boot();
