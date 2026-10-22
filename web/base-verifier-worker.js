self.onmessage = async ({ data }) => {
	try {
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
		const pointer = module._malloc(data.length);
		if (!pointer) throw Error("Cannot allocate launch base verification buffer.");
		try {
			module.HEAPU8.set(data, pointer);
			const response = module.ccall(
				"phoenix_verify_launch_base",
				"string",
				["number", "number"],
				[pointer, data.length],
			);
			self.postMessage(JSON.parse(response));
		} finally {
			module._free(pointer);
		}
	} catch (error) {
		self.postMessage({ ok: false, error: String(error.message || error) });
	}
};
