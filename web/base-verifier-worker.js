self.onmessage = async ({ data }) => {
	try {
		const archives = data.migration || (Array.isArray(data) ? data : [data]);
		if (!archives.length || archives.length > 32) throw Error("Invalid pack selection.");
		for (const data of archives)
			if (!(data instanceof Uint8Array) || !data.length || data.length > 32 * 1024 * 1024)
				throw Error("Invalid launch base archive size.");
		self.postMessage({ phase: "Loading the mod checker…" });
		importScripts(new URL("phoenix_workshop_test.js", self.location.href).href);
		const module = await self.createPhoenixWorkshopTestModule({
			locateFile: (file) => new URL(file, self.location.href).href,
			setStatus: (message) => {
				if (!message) return;
				const match = /\((\d+(?:\.\d+)?)\/(\d+)\)/.exec(message);
				self.postMessage({
					phase: "Loading the mod checker…",
					received: match ? Number(match[1]) : undefined,
					total: match ? Number(match[2]) : undefined,
				});
			},
		});
		self.postMessage({ phase: "Checking base mod and reading levels…" });
		for (const archive of archives) {
			const pointer = module._malloc(archive.length);
			if (!pointer) throw Error("Cannot allocate verification buffer.");
			try {
				module.HEAPU8.set(archive, pointer);
				if (!Array.isArray(data) && !data.migration) {
					self.postMessage(
						JSON.parse(
							module.ccall(
								"phoenix_verify_launch_base",
								"string",
								["number", "number"],
								[pointer, archive.length],
							),
						),
					);
					return;
				}
				if (
					!module.ccall(
						"phoenix_workshop_test_blob",
						"number",
						["number", "number"],
						[pointer, archive.length],
					)
				)
					throw Error("Content selection exceeds memory limit.");
			} finally {
				module._free(pointer);
			}
		}
		if (data.migration) {
			const result = JSON.parse(module.ccall("phoenix_migrate_bundled_menu", "string", [], []));
			if (result.ok)
				result.preview = result.preview.map((menu, i) => {
					const size = module.ccall("phoenix_menu_blob_size", "number", ["number"], [i]);
					const at = module.ccall("phoenix_menu_blob_data", "number", ["number"], [i]);
					return { menu, bytes: module.HEAPU8.slice(at, at + size) };
				});
			self.postMessage(result);
		} else self.postMessage(JSON.parse(module.ccall("phoenix_verify_menu_packs", "string", [], [])));
	} catch (error) {
		self.postMessage({ ok: false, error: String(error.message || error) });
	}
};
