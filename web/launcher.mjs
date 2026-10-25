import { prepareRuntimeEnvironment } from "./runtime-environment.mjs";
import { startSessionPump } from "./session-pump.mjs";
import { acquireSessionProfile } from "./session-profile.mjs";
import { verifyLaunchBase } from "./base-verifier.mjs";
// Metadata is available independently; only entry awaits this verified selection.
export function stagedSelection({ selection, load, verify, accept }) {
	const jobs = new Map();
	const keyOf = (packs) => JSON.stringify(packs.map((p) => [p.id, p.hash]));
	const current = () =>
		selection()
			.filter((p) => p.enabled)
			.map((p) => ({ ...p }));
	async function ready() {
		const selected = current(),
			key = keyOf(selected);
		if (!jobs.has(key)) {
			const job = Promise.all(selected.map(load)).then(async (archives) => ({
				archives,
				value: await verify(archives, key),
			}));
			jobs.set(key, job);
			job.catch(() => jobs.delete(key));
		}
		let result;
		try {
			result = await jobs.get(key);
		} catch (error) {
			if (key !== keyOf(current())) return ready();
			throw error;
		}
		if (key !== keyOf(current())) return ready();
		accept(result, selected);
		return result.value;
	}
	return {
		ready,
		seed: (selected, archives, value) => jobs.set(keyOf(selected), Promise.resolve({ archives, value })),
	};
}
const globalPreferenceDefaults = {
	volume: 50,
	sfx_volume: 50,
	siren_volume: 50,
	master_volume: 50,
	tools_scale: 100,
	language: "en",
	pseudo: false,
	reduced_motion: false,
	dust_motes: true,
	reference_grid: true,
};
const globalPreferences = (data) =>
	Object.fromEntries(Object.entries(globalPreferenceDefaults).map(([key, value]) => [key, data[key] ?? value]));
const scopedPreferenceKeys = ["player_name", "avatar", "packs", "saves", "pending_base_migration"];
// Merge only the active namespaces, inside the IndexedDB read/write transaction.
// Other tabs may have saved another base or local profile since this tab loaded.
export function mergeScopedLibrary(latest, incoming, scope) {
	if (!scope) return { ...latest, ...incoming };
	const profile = scope.split("/")[0];
	const disk = {
		...latest,
		...incoming,
		base_profiles: {
			...(latest.base_profiles || incoming.base_profiles || {}),
			[scope]: Object.fromEntries(scopedPreferenceKeys.map((key) => [key, incoming[key]])),
		},
		global_profiles: {
			...(latest.global_profiles || incoming.global_profiles || {}),
			[profile]: globalPreferences(incoming),
		},
	};
	if (disk.base_profiles["default/phoenix-base"]) Object.assign(disk, disk.base_profiles["default/phoenix-base"]);
	if (disk.global_profiles.default) Object.assign(disk, disk.global_profiles.default);
	return disk;
}
export function menuLocation(page, base, profile) {
	const url = new URL(page);
	url.search = "";
	url.hash = "";
	url.searchParams.set("base", base);
	url.searchParams.set("profile", profile || "default");
	return url.href;
}
export function* runtimeLibraryFiles(library, files) {
	const referenced = new Set([
		...library.packs.map((pack) => `packs/${pack.hash}.zip`),
		...library.saves.map((save) => `saves/${save.id}.zip`),
	]);
	for (const entry of files) {
		const [key] = entry;
		if (key === "library.json" || (referenced.has(key) && /^(packs|saves)\/[a-f0-9]{32,64}\.zip$/.test(key)))
			yield entry;
	}
}
export function movePack(selection, hash, shift, metadata) {
	const candidate = selection.map((pack) => ({ ...pack }));
	const index = candidate.findIndex((pack) => pack.hash === hash),
		next = index + shift;
	if (index < 0 || candidate[index].locked || next <= 0 || next >= candidate.length)
		throw Error("The launch base must remain first.");
	[candidate[index], candidate[next]] = [candidate[next], candidate[index]];
	const positions = new Map(candidate.map((pack, i) => [pack.id, i]));
	for (const [i, pack] of candidate.entries()) {
		const parent = metadata.get(pack.hash)?.parent;
		if (parent && (!positions.has(parent) || positions.get(parent) >= i))
			throw Error("Parent mods must remain before their children.");
	}
	return candidate;
}
export function removePack(selection, hash, metadata) {
	const chosen = selection.find((pack) => pack.hash === hash);
	if (!chosen) throw Error("Unknown installed pack.");
	if (chosen.locked) throw Error("The launch base cannot be removed.");
	if (selection.some((pack) => metadata.get(pack.hash)?.parent === chosen.id))
		throw Error("Remove child mods before removing their parent.");
	return selection.filter((pack) => pack.hash !== hash);
}
export function installPack(selection, meta, hash, metadata, baseId) {
	if (!/^[a-z0-9._-]{1,128}$/.test(meta.id) || !meta.parent || meta.base || meta.id === baseId)
		throw Error("Import a child mod of the launched base.");
	if (selection.some((pack) => pack.hash === hash)) return selection;
	if (selection.some((pack) => pack.id === meta.id))
		throw Error("A pack with this ID is already installed. Remove it before importing another revision.");
	if (selection.length >= 256) throw Error("Pack library is full.");
	const seen = new Set([meta.id]);
	let parent = meta.parent;
	while (parent !== baseId) {
		if (seen.has(parent)) throw Error("Pack parent cycle.");
		seen.add(parent);
		const entry = selection.find((pack) => pack.id === parent);
		if (!entry) throw Error("Install the parent mod first: " + parent);
		parent = metadata.get(entry.hash)?.parent;
		if (!parent) throw Error("The mod belongs to another base.");
	}
	return [...selection, { hash, id: meta.id, enabled: false }];
}

// The pre-launch shell has no WASM yet. Use ordinary DOM controls with the
// same thresholds/repeat policy as the runtime's shared C++ navigator.
export function menuPadInput(state, pad, now) {
	const values = {
		left: Math.max(-(pad.axes?.[0] || 0), pad.buttons?.[14]?.pressed ? 1 : 0),
		right: Math.max(pad.axes?.[0] || 0, pad.buttons?.[15]?.pressed ? 1 : 0),
		up: Math.max(-(pad.axes?.[1] || 0), pad.buttons?.[12]?.pressed ? 1 : 0),
		down: Math.max(pad.axes?.[1] || 0, pad.buttons?.[13]?.pressed ? 1 : 0),
		activate: pad.buttons?.[0]?.pressed ? 1 : 0,
		cancel: pad.buttons?.[1]?.pressed ? 1 : 0,
		previous: pad.buttons?.[4]?.pressed ? 1 : 0,
		next: pad.buttons?.[5]?.pressed ? 1 : 0,
	};
	if (!state.ready) {
		if (Object.values(values).some((v) => Math.abs(v) > 0.15)) return [];
		state.ready = true;
	}
	state.held ||= {};
	const actions = [];
	for (const [key, value] of Object.entries(values)) {
		const held = (state.held[key] ||= { down: false, next: 0 });
		const down = value >= (held.down ? 0.15 : 0.25);
		if (down && (!held.down || (key !== "activate" && key !== "cancel" && now >= held.next))) {
			actions.push(key);
			held.next = now + (held.down ? 100 : 400);
		}
		held.down = down;
	}
	return actions;
}
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
	return { logoWidth: Math.min(300, Math.max(180, width - 740)) };
}
// Elapsed time survives DOM rebuilds and layout changes. Hidden windows do not advance it.
export function introPlayback(config) {
	let elapsed = 0,
		complete = false;
	return {
		finish() {
			complete = true;
		},
		frame(delta, { height, textHeight, hidden = false, reduced = false }) {
			if (reduced) complete = true;
			if (!hidden && !complete) elapsed += Math.max(0, delta);
			const proseSeconds = Math.max(config.prose_seconds, (height + textHeight) / 32);
			const arrival = Math.max(0, Math.min(1, (elapsed - proseSeconds) / config.arrival_seconds));
			if (arrival === 1) complete = true;
			return {
				elapsed,
				complete,
				proseVisible: !complete && elapsed < proseSeconds,
				proseY: complete ? -textHeight : height - (height + textHeight) * Math.min(1, elapsed / proseSeconds),
				arrival: complete ? 1 : arrival,
			};
		},
	};
}

// A spinner shows activity; only real stage/download changes reset the wait clock.
export function loadingFeedback(
	paint,
	clock = {
		now: () => Date.now(),
		setInterval: (callback, delay) => globalThis.setInterval(callback, delay),
		clearInterval: (timer) => globalThis.clearInterval(timer),
	},
) {
	let timer, started, updated, current;
	const tick = () =>
		paint({
			...current,
			elapsed: Math.floor((clock.now() - started) / 1000),
			idle: Math.floor((clock.now() - updated) / 1000),
		});
	return {
		update(phase, received, total) {
			const next = { phase, received, total };
			if (!current) {
				started = clock.now();
				timer = clock.setInterval(tick, 1000);
			}
			if (!current || JSON.stringify(next) !== JSON.stringify(current)) updated = clock.now();
			current = next;
			tick();
		},
		stop() {
			clock.clearInterval(timer);
			current = undefined;
		},
	};
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
	tools_scale: 100,
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
		base: o.base || "/launcher/base.zip",
		"menu-scope": o.menu_scope || "",
		"base-url": o.base_url || "base.zip",
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
	const dependencies = (p) =>
		metadata.get(p.hash)?.parent ? [metadata.get(p.hash).parent] : metadata.get(p.hash)?.dependencies || [];
	if (chosen.locked) throw Error("The launch base cannot be disabled.");
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
export function themeStyles(theme) {
	if (!theme) return "";
	if (theme.schema !== 1 || !theme.tokens || typeof theme.tokens !== "object") throw Error("Invalid UI theme");
	const t = theme.tokens;
	const colour = (key, fallback) => {
		const value = t[key] ?? fallback;
		if (!/^#[0-9a-f]{6}([0-9a-f]{2})?$/i.test(value)) throw Error("Invalid theme colour");
		return value;
	};
	const family = t.font_family ?? "LatoLatin";
	if (typeof family !== "string" || !family.length || family.length > 256 || /[;{}<>"\\:()\[\]@\r\n]/.test(family))
		throw Error("Invalid theme font");
	const size = t.font_size ?? 22;
	if (!Number.isFinite(size) || size < 12 || size > 128) throw Error("Invalid theme font size");
	const numeric = (key, fallback, maximum) => {
		const value = t[key] ?? fallback;
		if (!Number.isFinite(value) || value < 0 || value > maximum) throw Error("Invalid theme dimension");
		return value;
	};
	const fallbacks = [];
	let css = `body {color:${colour("text", "#fff0d4")};background-color:${colour("background", "#08131b")};font-family:${family};font-size:${size}px;}
	button,input,select,.skin {color:${colour("text", "#fff0d4")};background-color:${colour("panel", "#102027")};border-color:${colour("border", "#988b71")};}
	button,input,select {border-radius:${numeric("radius", 4, 128)}px;border-width:${numeric("border_width", 1, 32)}px;padding:${numeric("gap", 12, 128) / 2}px;}
	.selected {border-color:${colour("accent", "#d6ac64")};}.muted {color:${colour("muted", "#b6c2c8")};}
	button:focus-visible,input:focus-visible,.selected {outline:2px solid ${colour("focus", "#ffe4b0")};}
	button:disabled,input:disabled {color:${colour("disabled", "#64717b")};}
	.error {color:${colour("error", "#f08070")};}`;
	for (const [slot, source] of Object.entries(theme.asset_data || {})) {
		if (
			typeof source !== "string" ||
			source.length > 24 * 1024 * 1024 ||
			!/^data:(image\/png|font\/ttf);base64,[a-z0-9+/]*={0,2}$/i.test(source)
		)
			throw Error("Invalid theme asset");
		if (slot === "font" || /^fallback-[1-9][0-9]*$/.test(slot)) {
			if (!source.startsWith("data:font/ttf;")) throw Error("Invalid theme font asset");
			const name = slot === "font" ? family : `PhoenixThemeFallback${fallbacks.length}`;
			if (slot !== "font") fallbacks.push(name);
			css += `@font-face {font-family:"${name}";src:url("${source}");font-display:swap;}`;
			continue;
		}
		if (!["background", "panel", "button"].includes(slot)) continue;
		const selector =
			slot === "background" ? "body" : slot === "button" ? "button,.skin" : ".column,.menu-column,.panel";
		const slices = theme.slices?.[slot];
		if (slices) {
			if (
				!Array.isArray(slices) ||
				slices.length !== 4 ||
				slices.some((v) => !Number.isInteger(v) || v < 0 || v > 16384)
			)
				throw Error("Invalid nine-slice");
			css += `${selector} {border-image-source:url("${source}");border-image-slice:${slices.join(" ")} fill;border-image-width:${slices.map((v) => `${v}px`).join(" ")};}`;
		} else css += `${selector} {background-image:url("${source}");background-size:cover;}`;
	}
	if (fallbacks.length) css += `body {font-family:${family},${fallbacks.join(",")},sans-serif;}`;
	return css;
}

export function menuPreview(source) {
	const text = (value, limit = 256) => {
		if (typeof value !== "string" || !value.length || value.length > limit)
			throw Error("Invalid menu preview text.");
		return value;
	};
	const list = (value, max) => {
		if (!Array.isArray(value) || value.length > max) throw Error("Invalid menu preview list.");
		return value;
	};
	if (source.schema !== 1) throw Error("Unsupported menu preview.");
	return {
		schema: 1,
		hash: source.hash,
		sidecar_version: source.sidecar_version,
		sources: source.sources,
		clients: source.clients,
		overrides: source.overrides,
		ships: source.ships,
		id: source.id === undefined ? "" : text(source.id),
		name: source.name === undefined ? "" : text(source.name),
		dependencies: list(source.parent ? [source.parent] : source.dependencies || [], 32).map((value) => text(value)),
		parent: source.parent,
		base: source.base,
		theme: source.theme,
		scenarios: list(source.scenarios, 1024).map((scene) => ({
			id: text(scene.id),
			name: text(scene.name || scene.id),
			slots: list(scene.slots, 64).map((slot) => ({
				id: text(slot.id),
				default_ship: slot.default_ship === undefined ? "" : text(slot.default_ship),
				ships: list(slot.ships, 256).map((ship) => ({
					id: text(ship.id),
					name: text(ship.name || ship.id),
					briefing: ship.briefing ? text(ship.briefing, 4096) : "",
				})),
			})),
		})),
	};
}
// Bounded preview extraction only. The production ZIP/content loader verifies it again.
export async function zipEntry(bytes, name, optional = false) {
	// DAT retains bounded text metadata for the pre-WASM launcher; gameplay definitions are binary.
	if (new TextDecoder().decode(bytes.subarray(0, 8)) === "PHXDAT1\n") bytes = bytes.subarray(8);
	if (bytes.length > 64 * 1024 * 1024) throw Error("Archive exceeds 64 MiB.");
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
				packed > 64 * 1024 * 1024 ||
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
	const runtime = await prepareRuntimeEnvironment().catch((error) => {
		document.getElementById("status").textContent = String(error);
		return null;
	});
	if (!runtime || runtime.reloading) return;
	const $ = (id) => document.getElementById(id);
	const status = (message) => {
		$("status").textContent = message;
	};
	let scopeKey = "",
		launchBase,
		launchBaseBytes;
	let baseUrl;
	const packJobs = new Map();
	let migrationJob,
		previousBase,
		verifiedRoot = false;
	const selectionKey = () => JSON.stringify(library.packs.filter((p) => p.enabled).map((p) => [p.id, p.hash]));
	const progress = (...args) => {
		if (busy || loading) loadingStage(...args);
	};
	async function loadArchive(pack) {
		if (packJobs.has(pack.hash)) return packJobs.get(pack.hash);
		const job = (async () => {
			const key = `packs/${pack.hash}.zip`;
			let bytes = files.get(key);
			if (!bytes) bytes = await request(db.transaction("files").objectStore("files").get(key));
			if (!bytes) {
				if (!pack.url) throw Error("Installed pack archive is missing: " + pack.id);
				bytes = await fetchFile(pack.url, undefined, "no-cache", (received, total) =>
					progress("Downloading mod pack:", received, total, pack.id),
				);
			}
			if (hex(await crypto.subtle.digest("SHA-256", bytes)) !== pack.hash)
				throw Error("Published content changed. Reload the menu.");
			await write([[key, bytes]]);
			return bytes;
		})();
		packJobs.set(pack.hash, job);
		job.catch(() => packJobs.delete(pack.hash));
		return job;
	}
	const staged = stagedSelection({
		selection: () => library.packs,
		load: loadArchive,
		verify: (archives, key) =>
			verifyLaunchBase(archives, undefined, ({ phase, received, total }) => {
				if (key === selectionKey()) progress(phase, received, total);
			}),
		accept: ({ archives }) => {
			launchBaseBytes = archives[0];
		},
	});
	function prepareSelection() {
		const promise = (async () => {
			if (migrationJob) await migrationJob;
			return staged.ready();
		})();
		promise.catch((error) => {
			if (!busy) report(error);
		});
		return promise;
	}

	let library = defaults(),
		nebulaFlashes,
		menuAmbient,
		menuIntro,
		db,
		busy = false,
		loading = true,
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
	let loadingActive = false;
	const feedback = loadingFeedback(({ phase, received, total, elapsed, idle }) => {
		$("startup-loading").hidden = false;
		$("startup-phase").textContent = phase;
		$("startup-time").textContent = `${t("Elapsed")}: ${elapsed}s`;
		if (received !== undefined) {
			const mb = (value) => (value / (1024 * 1024)).toFixed(1);
			$("startup-time").textContent += ` · ${mb(received)} MB` + (total > 0 ? ` / ${mb(total)} MB` : "");
		}
		$("startup-wait").textContent =
			idle >= 20
				? t("Waiting for the next update.") +
					` ${idle}s. ` +
					t(
						"Downloads and content checks can take longer on phones. If this does not change, reload and try again.",
					)
				: t("Preparing your mods and levels. First loads may take longer.");
		$("startup-reload").hidden = idle < 30;
		const bar = $("startup-progress");
		if (total > 0 && received !== undefined) {
			bar.max = total;
			bar.value = received;
		} else bar.removeAttribute("value");
	});
	const loadingStage = (message, received, total, detail = "") => {
		loadingActive = true;
		feedback.update(t(message) + (detail ? " " + detail : ""), received, total);
	};
	const finishLoading = () => {
		loadingActive = false;
		feedback.stop();
		$("startup-loading").hidden = true;
	};
	$("startup-reload").onclick = () => location.reload();
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
		el.disabled = busy || loading;
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
		const pending = changes.map(([key, value]) => [key, value]);
		const tx = db.transaction("files", "readwrite"),
			store = tx.objectStore("files");
		let merged;
		const done = new Promise((resolve, reject) => {
			tx.oncomplete = resolve;
			tx.onabort = () => reject(tx.error || Error("Browser storage failed."));
			tx.onerror = () => {};
		});
		for (const entry of pending) {
			const [key, value] = entry;
			if (key === "library.json" && value !== null) {
				const read = store.get(key);
				read.onsuccess = () => {
					try {
						const previous = read.result ? JSON.parse(decoder.decode(read.result)) : {};
						merged = mergeScopedLibrary(previous, JSON.parse(decoder.decode(value)), scopeKey);
						entry[1] = encoder.encode(JSON.stringify(merged));
						store.put(entry[1], key);
					} catch (error) {
						tx.abort();
					}
				};
			} else if (value === null) store.delete(key);
			else store.put(value, key);
		}
		await done;
		for (const [key, value] of pending)
			if (value === null) files.delete(key);
			else files.set(key, value);
		return merged;
	}
	const globalDefaults = globalPreferenceDefaults;
	const globals = globalPreferences;
	async function remember(changes = []) {
		try {
			const disk = await write([...changes, ["library.json", encoder.encode(JSON.stringify(library))]]);
			library.base_profiles = disk.base_profiles;
			library.global_profiles = disk.global_profiles;
			savedLibrary = JSON.stringify(library);
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
	async function fetchFile(url, limit = 64 * 1024 * 1024, cache = "default", onProgress) {
		const response = await fetch(url, { cache });
		if (!response.ok) throw Error("Could not load " + url);
		if (+response.headers.get("content-length") > limit) throw Error("Download exceeds size limit.");
		const reader = response.body.getReader(),
			chunks = [];
		let size = 0;
		const total = Number(response.headers.get("content-length")) || undefined;
		onProgress?.(0, total);
		for (;;) {
			const { value, done } = await reader.read();
			if (done) break;
			size += value.length;
			onProgress?.(size, total);
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

	async function derivePreview(bytes) {
		const manifest = JSON.parse(decoder.decode(await zipEntry(bytes, "manifest.json")));
		const ancestors = [],
			visiting = new Set([manifest.id]);
		async function append(id) {
			if (visiting.has(id)) throw Error("Mod parent cycle.");
			visiting.add(id);
			const parent = library.packs.find((p) => p.id === id);
			if (!parent) throw Error("Install the parent mod first: " + id);
			const archive = await loadArchive(parent);
			const meta = JSON.parse(decoder.decode(await zipEntry(archive, "manifest.json")));
			if (meta.parent) await append(meta.parent);
			ancestors.push(archive);
		}
		if (manifest.parent) await append(manifest.parent);
		return menuPreview(await verifyLaunchBase([...ancestors, bytes]));
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
	let appliedTheme = "";
	function render() {
		const selectedTheme = [...library.packs]
			.reverse()
			.filter((p) => p.enabled)
			.map((p) => previews.get(p.hash)?.theme)
			.find(Boolean);
		const themeIdentity = JSON.stringify(selectedTheme);
		if (themeIdentity !== appliedTheme) {
			let style = document.getElementById("pack-theme");
			if (!style) {
				style = document.createElement("style");
				style.id = "pack-theme";
				document.head.append(style);
			}
			try {
				style.textContent = themeStyles(selectedTheme);
			} catch {
				style.textContent = "";
			}
			appliedTheme = themeIdentity;
		}
		updateProfile();
		$("columns").setAttribute("aria-busy", String(loading));
		for (const id of ["settings", "packs", "profile-crt"]) $(id).disabled = busy || loading;
		document.body.classList.toggle("cascading", branch !== "root");
		for (const input of $("columns").querySelectorAll("input[name]")) draftFields.set(input.name, input.value);
		if (branch === "ships" && !scenarios().some((scene) => scene.id === scenario)) {
			branch = "scenarios";
			scenario = "";
		}
		document.body.classList.toggle("reduced-motion", library.reduced_motion);
		nebulaFlashes?.sync();
		menuAmbient?.sync();
		menuIntro?.sync();
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
			form.onchange = run(async () => {
				await remember();
				if (library.profile !== scopeKey.split("/")[0])
					location.assign(menuLocation(location.href, baseUrl, library.profile));
			});
			if (branch !== "host") {
				if (scenarios().length !== 1) {
					const maps = column("Scenarios");
					element("h2", "Scenarios", maps);
					if (!scenarios().length)
						element(
							"p",
							loading ? "Loading mods and levels…" : "Enable mod packs to choose a scenario.",
							maps,
						);
					for (const scene of scenarios())
						button(maps, scene.name || scene.id, () => choose("ships", scene.id), scene.id === scenario);
				}
				if (branch === "ships") {
					const ships = column("Ships"),
						scene = scenarios().find((s) => s.id === scenario);
					button(ships, "GM", () => start(options({ type: "server", action: "host-gm", scenario })));
					for (const slot of scene?.slots || []) {
						element("h2", slot.id, ships);
						for (const ship of slot.ships) {
							button(ships, ship.name || ship.id, () =>
								start(
									options({ type: "server", action: "host", scenario, slot: slot.id, ship: ship.id }),
								),
							);
						}
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
		menuIntro?.finish();
		const popup = $("popup");
		if (!popup.open) popupFocus = document.activeElement;
		popup.replaceChildren();
		const heading = element("h2", title, popup);
		heading.id = "popup-title";
		if (!popup.open) popup.showModal();
		menuAmbient?.sync();
		return popup;
	}
	function openingCrawl(config) {
		const prose = $("intro-prose"),
			title = $("title"),
			skip = $("skip-intro"),
			read = $("read-intro");
		let textKey;
		const localiseIntro = () => {
			const paragraphs = config.paragraphs.map(t);
			const key = paragraphs.join("\n");
			if (key !== textKey) {
				prose.replaceChildren();
				for (const text of paragraphs) element("p", undefined, prose).textContent = text;
				// The absolute title follows the prose without contributing to its measured height.
				prose.append(title);
				textKey = key;
			}
			read.textContent = t("Read introduction");
			skip.textContent = t("Skip intro");
			return paragraphs;
		};
		read.hidden = false;
		const playback = introPlayback(config);
		const preference = matchMedia("(prefers-reduced-motion: reduce)");
		let last, request;
		const paint = (delta = 0) => {
			localiseIntro();
			const height = $("menu").clientHeight;
			const frame = playback.frame(delta, {
				height,
				textHeight: prose.offsetHeight,
				hidden: document.hidden,
				reduced: library.reduced_motion || preference.matches,
			});
			prose.style.top = `${frame.proseY}px`;
			for (const paragraph of prose.children) {
				if (paragraph === title) continue;
				const y = frame.proseY + paragraph.offsetTop;
				paragraph.style.opacity = Math.max(0, Math.min(1, (y + paragraph.offsetHeight) / (height * 0.25)));
			}
			const eased = 1 - (1 - frame.arrival) ** 2;
			title.style.visibility = "visible";
			$("intro-plane").style.transform = `rotateX(${35 * (1 - eased)}deg)`;
			const skipFocused = document.activeElement === skip;
			skip.hidden = frame.complete;
			if (frame.complete && skipFocused) read.focus();
			return frame.complete;
		};
		const tick = (now) => {
			const done = paint(last === undefined ? 0 : (now - last) / 1000);
			last = now;
			if (!done) request = requestAnimationFrame(tick);
		};
		const finish = () => {
			playback.finish();
			cancelAnimationFrame(request);
			paint();
		};
		skip.onclick = finish;
		read.onclick = () => {
			const popup = dialog("Introduction");
			const text = element("div", undefined, popup);
			text.id = "intro-reading";
			for (const paragraph of localiseIntro()) element("p", undefined, text).textContent = paragraph;
			text.lastElementChild.append(" " + t(config.ending));
			button(popup, "Back", () => popup.close()).focus();
		};
		document.addEventListener("visibilitychange", () => {
			last = undefined;
		});
		window.addEventListener("resize", () => paint());
		preference.addEventListener("change", () => paint());
		paint();
		request = requestAnimationFrame(tick);
		return { finish, sync: () => paint() };
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
		if (loading) {
			element("p", "Reading installed mod packs…", popup);
			return;
		}
		element(
			"p",
			"Pack choices are verified by C++ when entering the lobby. Active sessions retain their exact content.",
			popup,
		).className = "help";
		button(popup, "Import ZIP Pack", () => importFile("pack"));
		for (const [index, pack] of library.packs.entries()) {
			const row = element("div", undefined, popup);
			row.className = "pack-row";
			const seen = new Set();
			let ancestor = previews.get(pack.hash)?.parent,
				depth = 0;
			while (ancestor && !seen.has(ancestor)) {
				seen.add(ancestor);
				depth++;
				const entry = library.packs.find((p) => p.id === ancestor);
				ancestor = entry && previews.get(entry.hash)?.parent;
			}
			row.style.marginLeft = `${depth * 16}px`;
			const name = previews.get(pack.hash)?.name || pack.id;
			const label = element("label", undefined, row);
			const toggle = element("input", undefined, label);
			toggle.type = "checkbox";
			toggle.checked = pack.enabled;
			toggle.disabled = !!pack.locked;
			element("span", name, label);
			toggle.onchange = run(async () => {
				library.packs = togglePack(library.packs, pack.hash, previews);
				await remember();
				void prepareSelection();
				render();
				packs();
			});
			for (const [symbol, title, shift] of [
				["↑", "Move Up", -1],
				["↓", "Move Down", 1],
			]) {
				const move = button(row, symbol, async () => {
					library.packs = movePack(library.packs, pack.hash, shift, previews);
					await remember();
					void prepareSelection();
					packs();
				});
				move.setAttribute("aria-label", t(title) + ": " + name);
				move.disabled = !!pack.locked || index + shift <= 0 || index + shift >= library.packs.length;
			}
			const remove = button(row, "🗑", async () => {
				library.packs = removePack(library.packs, pack.hash, previews);
				await remember();
				void prepareSelection();
				render();
				packs();
			});
			remove.disabled = !!pack.locked;
			remove.setAttribute("aria-label", t("Remove from Library") + ": " + name);
		}

		button(popup, "Back", () => popup.close());
	}
	async function importFile(kind) {
		const input = document.createElement("input");
		input.type = "file";
		input.accept = ".zip,.dat,application/zip,application/octet-stream";
		input.onchange = run(async () => {
			const file = input.files[0];
			if (!file) return;
			const limit = (kind === "pack" ? 64 : 128) * 1024 * 1024;
			if (file.size > limit)
				throw Error(kind === "pack" ? "Archive exceeds 64 MiB." : "Save archive exceeds 128 MiB.");
			const bytes = new Uint8Array(await file.arrayBuffer());
			if (kind === "pack") {
				const meta = await derivePreview(bytes),
					hash = hex(await crypto.subtle.digest("SHA-256", bytes));
				const next = installPack(library.packs, meta, hash, previews, launchBase.id);
				if (next !== library.packs) {
					library.packs = next;
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
		await prepareSelection();
		if (all) await Promise.all(library.packs.map(loadArchive));
	}

	function snapshotRuntime() {
		if (!window.Module?.FS || failed) return persist;
		const fs = window.Module.FS;
		try {
			const raw = fs.readFile("/launcher/menu/library.json", { encoding: "utf8" });
			if (raw === lastIndex) return persist;
			const diskIndex = JSON.parse(raw);
			const scoped = diskIndex.base_profiles?.[scopeKey];
			const global = diskIndex.global_profiles?.[scopeKey.split("/")[0]] || {};
			const index = mergeRuntimeLibrary(scoped ? { ...diskIndex, ...scoped, ...global } : diskIndex, library),
				changes = [["library.json", encoder.encode(JSON.stringify(index))]];
			const retained = new Set([
				...index.packs.map((pack) => `packs/${pack.hash}.zip`),
				...index.saves.map((save) => `saves/${save.id}.zip`),
			]);
			// Blobs may be retained by another base profile; library removal only unlinks them.
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
			for (const save of index.saves) {
				const key = `saves/${save.id}.zip`;
				try {
					changes.push([key, fs.readFile(`/launcher/menu/${key}`)]);
				} catch {
					if (!window.Module.phoenixConsoleOnly || !files.has(key))
						throw Error("Managed save archive is missing: " + save.id);
				}
			}
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
			location.assign(menuLocation(location.href, baseUrl, scopeKey.split("/")[0]));
		} catch {
			returning = false;
		}
	}
	async function start(o, direct = false) {
		menuIntro?.finish();
		if (!launchBase) throw Error("A verified launch base is required.");
		if (o.profile !== scopeKey.split("/")[0]) {
			const query = new URLSearchParams(o);
			query.set("base", baseUrl);
			query.set("relay", o.relay ? "1" : "0");
			query.set("textprobe", o.textprobe ? "1" : "0");
			location.assign(location.origin + location.pathname + "?" + query);
			return;
		}
		let profileLease;
		if (started || busy) return;
		busy = true;
		render();
		$("startup-title").textContent = t(o.action === "workshop" ? "Opening Workshop" : "Loading your game");
		loadingStage("Preparing selected mod packs…");
		$("settings").disabled = $("packs").disabled = true;
		try {
			if (o.type !== "client") {
				await ensurePacks(o.action === "workshop");
				for (const save of library.saves) {
					const key = `saves/${save.id}.zip`;
					if (!files.has(key)) {
						const bytes = await request(db.transaction("files").objectStore("files").get(key));
						if (bytes) files.set(key, bytes);
					}
				}
			}
			loadingStage("Saving your setup…");
			await remember();
			let tabStorage;
			try {
				tabStorage = window.sessionStorage;
			} catch {}
			loadingStage("Preparing your player profile…");
			profileLease = await acquireSessionProfile(o.profile, {
				locks: navigator.locks,
				storage: tabStorage,
				randomUUID: () => crypto.randomUUID(),
			});
			window.addEventListener("pagehide", (event) => {
				if (!event.persisted) profileLease.release();
			});
			// The chosen role runtime starts only after root verification and profile selection.
			started = true;
			if (!direct) {
				const query = new URLSearchParams(o);
				query.set("base", baseUrl);
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
			loadingStage("Downloading game files…");
			window.Module = {
				canvas: $("canvas"),
				phoenixConsoleOnly: o.type === "client",
				phoenixCanvasPoint: (x, y) =>
					canvasPoint(
						$("canvas").getBoundingClientRect(),
						$("canvas").width,
						$("canvas").height,
						document.documentElement.dataset.rotated === "true",
						x,
						y,
					),
				arguments: launchArguments(
					{
						...o,
						profile: profileLease.profile,
						base: "/launcher/base.zip",
						base_url: baseUrl,
						menu_scope: scopeKey,
					},
					location.origin + location.pathname,
				),
				print: console.log,
				printErr: console.error,

				preRun: [
					() => {
						loadingStage("Preparing game content and saved games…");
						const fs = window.Module.FS;
						fs.mkdirTree("/launcher/menu/packs");
						fs.mkdirTree("/launcher/menu/saves");
						if (o.type !== "client") fs.writeFile("/launcher/base.zip", launchBaseBytes);
						fs.writeFile("/launcher/base.zip.menu.json", encoder.encode(JSON.stringify(launchBase)));
						for (const pack of library.packs) {
							const meta = previews.get(pack.hash);
							if (meta)
								fs.writeFile(
									`/launcher/menu/packs/${pack.hash}.zip.menu.json`,
									encoder.encode(JSON.stringify(meta)),
								);
						}
						for (const [key, bytes] of runtimeLibraryFiles(library, files))
							if (o.type !== "client" || key === "library.json")
								fs.writeFile("/launcher/menu/" + key, bytes);
						lastIndex = decoder.decode(files.get("library.json"));
					},
				],
				onRuntimeInitialized: () => loadingStage("Starting the game and loading levels…"),
				phoenixStartupStage: (message) => {
					if (!failed && loadingActive) loadingStage(message);
				},
				phoenixStartupReady: () => {
					if (!failed) {
						finishLoading();
						status("");
						$("canvas").focus();
					}
				},
				onAbort: (reason) => launchError("Startup failed: " + String(reason || "Unknown runtime error")),
				setStatus: (message) => {
					if (!failed && loadingActive) {
						const match = /\((\d+(?:\.\d+)?)\/(\d+)\)/.exec(message);
						if (match && Number(match[2]) > 0)
							loadingStage("Downloading game files…", Number(match[1]), Number(match[2]));
						else
							loadingStage(
								/download/i.test(message)
									? "Downloading game files…"
									: "Starting the game and loading levels…",
							);
					}
				},
				phoenixPersistMenu: () => {
					return snapshotRuntime();
				},
				phoenixReturnToMenu: returnMenu,
				phoenixStartSessionPump: () => {
					const pump = () => window.Module.ccall("phoenix_session_pump", null, [], []);
					let stop = startSessionPump(pump);
					window.addEventListener("pagehide", () => stop());
					window.addEventListener("pageshow", (event) => {
						if (event.persisted) stop = startSessionPump(pump);
					});
				},
				phoenixLaunchError: launchError,
			};
			const script = document.createElement("script");
			script.src = runtime.script(o.type);
			script.onerror = () => launchError("Build files unavailable. Build and serve the web output first.");
			document.body.append(script);
		} catch (error) {
			finishLoading();
			profileLease?.release();
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
		finishLoading();
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
		button($("columns"), "Return to Menu", () =>
			location.assign(menuLocation(location.href, baseUrl, scopeKey.split("/")[0])),
		).disabled = false;
	}
	// Script evaluation and Emscripten's asynchronous run() can fail after start()
	// has returned. Surface those failures instead of leaving the loading panel up.
	window.addEventListener("error", (event) => {
		if (started && loadingActive && !failed && event.message)
			launchError(t("Startup failed:") + " " + event.message);
	});
	window.addEventListener("unhandledrejection", (event) => {
		if (started && loadingActive && !failed)
			launchError(t("Startup failed:") + " " + String(event.reason?.message || event.reason));
	});
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
	// Paint the shell before storage, archive downloads and WASM verification.
	// Entry stays disabled until the scoped library is ready.
	try {
		render();
		$("startup-title").textContent = t("Preparing main menu");
		loadingStage("Reading saved settings and mod packs…");
		baseUrl = new URL(new URLSearchParams(location.search).get("base") ?? "base.zip", location.href).href;
		const opening = indexedDB.open("phoenix-v3-menu", 1);
		opening.onupgradeneeded = () => opening.result.createObjectStore("files");
		db = await request(opening);
		db.onversionchange = () => db.close();
		// Read the index first. Historical archives and other profiles can be much
		// larger than the active library and must not all enter the page's heap.
		const cachedLibrary = await request(db.transaction("files").objectStore("files").get("library.json"));
		if (cachedLibrary !== undefined) files.set("library.json", cachedLibrary);
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
		loadingStage("Loading menu artwork and language…");
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
		loadingStage("Reading levels and ships...");
		try {
			launchBase = menuPreview(
				JSON.parse(decoder.decode(await fetchFile(baseUrl + ".menu.json", 1024 * 1024, "no-cache"))),
			);
			if (launchBase.sidecar_version !== 1 || !/^[a-f0-9]{64}$/.test(launchBase.hash))
				throw Error("Unsupported sidecar");
		} catch {
			launchBaseBytes = await fetchFile(baseUrl, undefined, "no-cache", (received, total) =>
				loadingStage("Downloading base mod and levels...", received, total),
			);
			launchBase = menuPreview(await verifyLaunchBase([launchBaseBytes]));
			verifiedRoot = true;
		}

		if (!launchBase.base || launchBase.parent || launchBase.dependencies.length)
			throw Error("Launch base must be an independent root.");
		if (!/^[a-z0-9._-]{1,128}$/.test(launchBase.id)) throw Error("Invalid base identity.");
		const mode = launchBase.base?.render_mode || "sprites",
			style = launchBase.base?.interface_style || "current";
		if (!["sprites", "models"].includes(mode) || style !== "current") throw Error("Unsupported base presentation.");
		const localProfile = routes?.profile || new URLSearchParams(location.search).get("profile") || "default";
		loadingStage("Restoring your mod selection and profile…");
		if (!/^[\w.-]{1,64}$/.test(localProfile))
			throw Error("Use letters, numbers, dots, underscores or hyphens in the local profile.");
		scopeKey = localProfile + "/" + launchBase.id;
		library.profile = scopeKey.split("/")[0];
		library.global_profiles ||= { default: globals(library) };
		Object.assign(library, library.global_profiles[scopeKey.split("/")[0]] || globalDefaults);
		library.base_profiles ||= {
			"default/phoenix-base": Object.fromEntries(scopedPreferenceKeys.map((k) => [k, library[k]])),
		};
		Object.assign(
			library,
			{ pending_base_migration: null },
			library.base_profiles[scopeKey] || { player_name: "<username>", avatar: [], packs: [], saves: [] },
		);
		previousBase = library.pending_base_migration || library.packs.find((p) => p.id === launchBase.id);
		const hash = launchBase.hash || hex(await crypto.subtle.digest("SHA-256", launchBaseBytes));
		if (launchBase.id === "phoenix-base" && previousBase && previousBase.hash !== hash)
			library.pending_base_migration = previousBase;
		library.packs = library.packs.filter(
			(p) => p.id !== launchBase.id && p.id !== "test" && p.id !== "phoenix-training",
		);
		library.packs.unshift({ id: launchBase.id, hash, enabled: true, locked: true, url: baseUrl });
		previews.set(hash, launchBase);
		if (verifiedRoot) staged.seed([library.packs[0]], [launchBaseBytes], launchBase);
		await remember([
			...(launchBaseBytes ? [[`packs/${hash}.zip`, launchBaseBytes]] : []),
			[`preview/${hash}.json`, encoder.encode(JSON.stringify(launchBase))],
		]);
		loadingStage("Reading installed mods and saves…");
		const stored = db.transaction("files").objectStore("files");
		const needed = new Set([...library.packs.map((pack) => `preview/${pack.hash}.json`)]);
		await Promise.all(
			[...needed]
				.filter((key) => !files.has(key))
				.map(async (key) => {
					const bytes = await request(stored.get(key));
					if (bytes !== undefined) files.set(key, bytes);
				}),
		);

		if (launchBase.id === "phoenix-base") {
			const url = new URL("test-missions.zip", baseUrl).href;
			try {
				let meta;
				try {
					meta = menuPreview(
						JSON.parse(decoder.decode(await fetchFile(url + ".menu.json", 1024 * 1024, "no-cache"))),
					);
					if (meta.sidecar_version !== 1 || !/^[a-f0-9]{64}$/.test(meta.hash))
						throw Error("Unsupported sidecar");
				} catch {
					const bytes = await fetchFile(url);
					const hash = hex(await crypto.subtle.digest("SHA-256", bytes));
					const verified = await verifyLaunchBase([await loadArchive(library.packs[0]), bytes]);
					meta = menuPreview(verified);
					await remember([[`packs/${hash}.zip`, bytes]]);
				}
				if (
					meta.id !== "phoenix-test-missions" ||
					meta.parent !== launchBase.id ||
					!/^[a-f0-9]{64}$/.test(meta.hash)
				)
					throw Error("Invalid bundled mod");
				const old = library.packs.find((p) => p.id === meta.id);
				if (!old || old.url) {
					library.packs = library.packs.filter((p) => p.id !== meta.id);
					library.packs.push({ id: meta.id, hash: meta.hash, enabled: old?.enabled || false, url });
				}
				previews.set(meta.hash, meta);
				await remember([[`preview/${meta.hash}.json`, encoder.encode(JSON.stringify(meta))]]);
				if (previousBase && previousBase.hash !== hash) {
					migrationJob = (async () => {
						const oldBytes = await request(
							db.transaction("files").objectStore("files").get(`packs/${previousBase.hash}.zip`),
						);
						if (!oldBytes)
							throw Error(
								"The previous installed base archive is missing; migration cannot preserve its edits.",
							);
						const inputs = [
							await loadArchive(library.packs[0]),
							await loadArchive({ id: meta.id, hash: meta.hash, url }),
							oldBytes,
						];
						if (old && !old.url) inputs.push(await loadArchive(old));
						const result = await verifyLaunchBase({ migration: inputs });
						const changes = [];
						for (const { menu, bytes } of result) {
							const current = library.packs.find((p) => p.id === menu.id);
							if (!current) continue;
							current.hash = menu.hash;
							if (menu.id === launchBase.id) {
								launchBase = menuPreview(menu);
								launchBaseBytes = bytes;
							}
							previews.set(menu.hash, menuPreview(menu));
							changes.push(
								[`packs/${menu.hash}.zip`, bytes],
								[`preview/${menu.hash}.json`, encoder.encode(JSON.stringify(menu))],
							);
						}
						library.pending_base_migration = null;
						await remember(changes);
						render();
					})();
					migrationJob.catch(report);
				}
			} catch (error) {
				if (library.pending_base_migration) {
					migrationJob = Promise.reject(error);
					migrationJob.catch(() => {});
				}
				report(error);
			}
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
			if (previews.has(pack.hash)) continue;
			loadingStage("Reading levels from mod pack:", undefined, undefined, pack.id);
			let meta;
			try {
				const cached = files.get(`preview/${pack.hash}.json`);
				meta = menuPreview(
					JSON.parse(
						decoder.decode(cached || (await fetchFile(pack.url + ".menu.json", 1024 * 1024, "no-cache"))),
					),
				);
				if (meta.sidecar_version !== 1 || meta.hash !== pack.hash || meta.id !== pack.id)
					throw Error("Invalid installed metadata");
			} catch {
				meta = await derivePreview(await loadArchive(pack));
			}
			previews.set(pack.hash, meta);
			await remember([[`preview/${pack.hash}.json`, encoder.encode(JSON.stringify(meta))]]);
		}

		if (routes) {
			loading = false;
			await start(routes, true);
			return;
		}
		await document.fonts.load('28px "Comic Neue"');
		await Promise.all(
			["cockpit-frame.png", "menu-logotype.png", "space-panorama.png", "menu-debris.png"].map(async (name) => {
				const image = new Image();
				image.src = `assets/menu/${name}`;
				await image.decode();
			}),
		);
		loading = false;
		render();
		finishLoading();
		menuIntro = openingCrawl(data[3].intro);
		void prepareSelection();
		if (!$("status").textContent || $("status").textContent === "Loading menu…") status("");
	} catch (error) {
		finishLoading();
		document.documentElement.classList.remove("direct-launch");
		$("menu").hidden = false;
		$("title").style.visibility = "visible";
		report(error);
		render();
	}
}
if (typeof document !== "undefined") boot();

if (
	typeof document !== "undefined" &&
	typeof navigator.getGamepads === "function" &&
	typeof requestAnimationFrame === "function"
) {
	const pads = new Map();
	let editing;
	const visible = (el) =>
		!el.disabled && !el.hidden && el.getClientRects().length && getComputedStyle(el).visibility !== "hidden";
	function navigateMenu(action) {
		const root = document.querySelector("#popup[open]") || document.getElementById("menu");
		const controls = Array.from(root.querySelectorAll("button,input,select,textarea")).filter(visible);
		if (!controls.length) return;
		let current = controls.includes(document.activeElement) ? document.activeElement : controls[0];
		if (editing && (!editing.el.isConnected || !controls.includes(editing.el))) editing = undefined;
		if (action === "cancel") {
			if (editing) {
				editing.el.value = editing.original;
				editing = undefined;
				return;
			}
			const popup = document.querySelector("#popup[open]");
			if (popup) {
				popup.close();
				return;
			}
			const back = document.getElementById("back");
			if (visible(back)) back.click();
			return;
		}
		if (action === "activate") {
			if (editing) {
				editing.el.dispatchEvent(new Event("input", { bubbles: true }));
				editing.el.dispatchEvent(new Event("change", { bubbles: true }));
				editing = undefined;
			} else if (current.matches('input[type="range"],input[type="number"]'))
				editing = { el: current, original: current.value };
			else {
				current.focus();
				if (current.matches("button,input[type=checkbox]")) current.click();
			}
			return;
		}
		if (editing && ["left", "right", "up", "down"].includes(action)) {
			const el = editing.el,
				step = Number(el.step) || 1,
				sign = action === "right" || action === "up" ? 1 : -1;
			el.value = String(
				Math.max(
					el.min === "" ? -1e9 : Number(el.min),
					Math.min(el.max === "" ? 1e9 : Number(el.max), Number(el.value) + sign * step),
				),
			);
			return;
		}
		let next;
		if (action === "previous" || action === "next")
			next =
				controls[
					(controls.indexOf(current) + controls.length + (action === "next" ? 1 : -1)) % controls.length
				];
		else {
			const a = current.getBoundingClientRect(),
				dx = action === "left" ? -1 : action === "right" ? 1 : 0,
				dy = action === "up" ? -1 : action === "down" ? 1 : 0;
			let best = Infinity;
			for (const c of controls) {
				const b = c.getBoundingClientRect(),
					x = b.x + b.width / 2 - a.x - a.width / 2,
					y = b.y + b.height / 2 - a.y - a.height / 2;
				const score = Math.hypot(x, y) + 2 * Math.abs(dx ? y : x);
				if (x * dx + y * dy > 1 && score < best) {
					best = score;
					next = c;
				}
			}
		}
		(next || current).focus();
		(next || current).scrollIntoView({ block: "nearest", inline: "nearest" });
	}
	function poll(now) {
		if (document.hidden || !document.hasFocus() || document.body.classList.contains("running")) {
			pads.clear();
			if (editing) editing.el.value = editing.original;
			editing = undefined;
		} else {
			const seen = new Set();
			for (const pad of navigator.getGamepads?.() || [])
				if (pad?.connected) {
					seen.add(pad.index);
					if (!pads.has(pad.index)) pads.set(pad.index, {});
					for (const action of menuPadInput(pads.get(pad.index), pad, now)) navigateMenu(action);
				}
			for (const id of pads.keys())
				if (!seen.has(id)) {
					pads.delete(id);
					if (editing) {
						editing.el.value = editing.original;
						editing = undefined;
					}
				}
		}
		requestAnimationFrame(poll);
	}
	requestAnimationFrame(poll);
}
