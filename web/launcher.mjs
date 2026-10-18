// The HTML shell gathers launch hints. C++ verifies content, setup and admission.
const encoder = new TextEncoder(),
	decoder = new TextDecoder("utf-8", { fatal: true });
const defaults = () => ({
	schema: 1,
	volume: 50,
	language: "en",
	pseudo: false,
	reduced_motion: false,
	dust_motes: true,
	reference_grid: true,
	player_name: "Player",
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
	if (!["host", "recover", "join", "workshop", "legacy"].includes(action)) throw Error("Unknown launch action.");
	if (
		((action === "host" || action === "recover" || action === "legacy") && type !== "server") ||
		(action === "workshop" && role !== "editor") ||
		(action === "join" && role === "editor")
	)
		throw Error("Conflicting launch action.");
	if (action === "join" && !/^(?:[A-Z0-9]{6}|[\w.-]+\/[\w.-]+)$/.test(code))
		throw Error("Enter the host's six-character Join Code.");
	if (action === "host" && (!scenario || !slot || !ship)) throw Error("Choose a scenario and ship first.");
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
export function automaticHostChoice(scenes, selected = "") {
	const scene = selected ? scenes.find((entry) => entry.id === selected) : scenes.length === 1 ? scenes[0] : null;
	if (scene?.slots.length !== 1 || scene.slots[0].ships.length !== 1) return null;
	return { scenario: scene.id, slot: scene.slots[0].id, ship: scene.slots[0].ships[0].id };
}
export function mergeRuntimeLibrary(index, previous) {
	return {
		...index,
		service: previous.service,
		profile: previous.profile,
		packs: index.packs.map((pack) => ({ ...previous.packs.find((old) => old.hash === pack.hash), ...pack })),
	};
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
		scenarios: list(source.scenarios, 1024).map((scene) => ({
			id: text(scene.id),
			name: text(scene.name || scene.id),
			slots: list(scene.slots, 64).map((slot) => ({
				id: text(slot.id),
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
		} catch (error) {
			library = JSON.parse(savedLibrary);
			throw error;
		}
	}
	async function fetchFile(url, limit = 32 * 1024 * 1024) {
		const response = await fetch(url);
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
		if (declared) return menuPreview({ ...JSON.parse(decoder.decode(declared)), id: manifest.id });
		// Older packs can expose direct authored slots; inherited setup is resolved in C++.
		const definitions = JSON.parse(decoder.decode(await zipEntry(bytes, "definitions.json")));
		return menuPreview({
			schema: 1,
			id: manifest.id,
			scenarios: (definitions.maps || [])
				.filter((map) => Array.isArray(map.ship_slots))
				.map((map) => ({
					id: map.id,
					name: map.name || map.id,
					slots: map.ship_slots.map((slot) => ({
						id: slot.id,
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
			const automatic = automaticHostChoice(scenes, next === "ships" ? id : "");
			if (automatic) return start(options({ type: "server", action: "host", ...automatic }));
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
		const name = field(parent, "player_name", "Player name", library.player_name || "Player");
		name.required = true;
		name.autocomplete = "nickname";
		name.onchange = run(async () => {
			const value = name.value.trim();
			if (!value || encoder.encode(value).length > 64 || /[\x00-\x1f\x7f]/.test(value))
				throw Error("Enter a player name of up to 64 bytes without control characters.");
			library.player_name = value;
			await remember();
		});
		if (library.avatar?.length) {
			const image = element("img", undefined, parent);
			image.src = "data:image/png;base64," + btoa(String.fromCharCode(...library.avatar));
			image.alt = "Player avatar";
			image.width = image.height = 48;
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
	function connection(parent, join = true) {
		const form = element("form", undefined, parent);
		if (join) {
			profileFields(form);
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
		for (const input of $("columns").querySelectorAll("input[name]")) draftFields.set(input.name, input.value);
		if (branch === "ships" && !scenarios().some((scene) => scene.id === scenario)) {
			branch = "scenarios";
			scenario = "";
		}
		document.body.classList.toggle("reduced-motion", library.reduced_motion);
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
					for (const slot of scene?.slots || []) {
						element("h2", slot.id, ships);
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
				library.player_name = String(p.get("player_name") || "Player").trim();
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
		return popup;
	}
	function settings() {
		const popup = dialog("Settings"),
			volume = field(popup, "volume", "Music volume", library.volume, "range");
		volume.min = "0";
		profileFields(popup);
		volume.max = "100";
		const value = element("output", library.volume + "%", popup);
		value.htmlFor = volume.id;
		volume.oninput = run(async () => {
			library.volume = +volume.value;
			value.textContent = library.volume + "%";
			await remember();
		});
		const language = element("label", "Language", popup),
			select = element("select", undefined, language);
		select.setAttribute("aria-label", t("Language"));
		const english = element("option", "English", select);
		english.value = "en";
		button(popup, "Pseudo-localisation: " + (library.pseudo ? "On" : "Off"), async () => {
			library.pseudo = !library.pseudo;
			await remember();
			settings();
		});
		button(popup, "Reduced motion: " + (library.reduced_motion ? "On" : "Off"), async () => {
			library.reduced_motion = !library.reduced_motion;
			await remember();
			settings();
		});
		for (const [key, label] of [
			["dust_motes", "Dust motes"],
			["reference_grid", "Reference grid"],
		])
			button(popup, label + ": " + (library[key] !== false ? "On" : "Off"), async () => {
				library[key] = library[key] === false;
				await remember();
				settings();
			});
		if (library.reduced_motion) element("p", "Reduced motion hides dust motes and the reference grid.", popup);
		button(popup, "Back", () => $("popup").close());
		volume.focus();
	}
	function packs() {
		const popup = dialog("Mod Packs");
		element(
			"p",
			"Pack choices are verified by C++ when entering the lobby. Active sessions retain their exact content.",
			popup,
		).className = "help";
		button(popup, "Import ZIP Pack", () => importFile("pack"));
		for (const pack of library.packs) {
			element("h2", pack.id, popup);
			button(popup, pack.enabled ? "Enabled" : "Disabled", async () => {
				pack.enabled = !pack.enabled;
				await remember();
				packs();
			});
			const row = element("div", undefined, popup);
			row.className = "row";
			const index = library.packs.indexOf(pack);
			for (const [name, shift] of [
				["Move Up", -1],
				["Move Down", 1],
			])
				if (index + shift >= 0 && index + shift < library.packs.length)
					button(row, name, async () => {
						[library.packs[index], library.packs[index + shift]] = [
							library.packs[index + shift],
							library.packs[index],
						];
						await remember();
						packs();
					});
			button(row, "Remove from Library", async () => {
				library.packs.splice(index, 1);
				await remember([
					[`packs/${pack.hash}.zip`, null],
					[`preview/${pack.hash}.json`, null],
				]);
				packs();
			});
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
				const bytes = await fetchFile(pack.url);
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
			if (o.action === "host" || o.action === "legacy" || o.action === "workshop")
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
			status(t("Loading application..."));
			window.Module = {
				canvas: $("canvas"),
				arguments: launchArguments(o, location.origin + location.pathname),
				print: console.log,
				printErr: console.error,
				preRun: [
					() => {
						const fs = window.Module.FS;
						fs.mkdirTree("/launcher/menu/packs");
						fs.mkdirTree("/launcher/menu/saves");
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
				onAbort: () => launchError("Startup failed. See the browser console for diagnostics."),
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
		status(message);
		$("columns").replaceChildren();
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
	$("settings").onclick = settings;
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
			library = JSON.parse(decoder.decode(files.get("library.json")));
			if (library.schema !== 1 || !Array.isArray(library.packs) || !Array.isArray(library.saves))
				throw Error("Invalid menu library. Existing browser data was preserved.");
		}
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
		]);
		translations = data[0].languages.en;
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
		if (routes) {
			await start(routes, true);
			return;
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
		if (!files.has("published-base-catalog.json")) {
			try {
				const response = await fetch("base.zip.menu.json");
				if (!response.ok) throw Error("Base game content unavailable. Import a pack in Mod Packs.");
				const meta = await response.json();
				menuPreview(meta);
				if (!/^[a-f0-9]{64}$/.test(meta.hash)) throw Error("Invalid published pack identity.");
				if (!library.packs.some((pack) => pack.id === meta.id))
					library.packs.push({
						hash: meta.hash,
						id: meta.id,
						enabled: library.packs.length === 0,
						url: "base.zip",
					});
				previews.set(meta.hash, meta);
				await remember([
					[`preview/${meta.hash}.json`, encoder.encode(JSON.stringify(meta))],
					["published-base-catalog.json", encoder.encode(meta.hash)],
				]);
			} catch (error) {
				status(t(error.message));
			}
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
