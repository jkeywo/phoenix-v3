export function runtimeScript(capabilities, isolated, role) {
	if (!["server", "client", "workshop"].includes(role)) throw Error("Unknown runtime role");
	if (capabilities.root_threaded && !isolated)
		throw Error(
			"This worker build requires an isolated HTTPS or localhost page with WebAssembly exception support. Use the ordinary browser build here.",
		);
	return (isolated && capabilities.threaded_directory ? "threaded/" : "") + role + ".js";
}

export async function prepareRuntimeEnvironment() {
	const response = await fetch(new URL("./runtime-capabilities.json", import.meta.url), { cache: "no-store" });
	const capabilities = response.ok ? await response.json() : {};
	const exceptions = () => typeof WebAssembly.Exception === "function" && typeof WebAssembly.Tag === "function";
	const supported = () =>
		globalThis.crossOriginIsolated === true && typeof SharedArrayBuffer !== "undefined" && exceptions();
	if ((capabilities.root_threaded || capabilities.threaded_directory) && !supported()) {
		try {
			if (!exceptions() || !globalThis.isSecureContext || !navigator.serviceWorker)
				throw Error("Isolation is unavailable");
			const install = async () => {
				const registration = await navigator.serviceWorker.register(
					new URL("./isolation-worker.js", import.meta.url),
					{ updateViaCache: "none" },
				);
				const worker = registration.installing || registration.waiting;
				if (worker && worker.state !== "activated")
					await new Promise((resolve, reject) => {
						worker.addEventListener("statechange", () => {
							if (worker.state === "activated") resolve();
							if (worker.state === "redundant") reject(Error("Isolation worker did not activate"));
						});
					});
				await navigator.serviceWorker.ready;
			};
			await Promise.race([
				install(),
				new Promise((_, reject) => setTimeout(() => reject(Error("Isolation timed out")), 4000)),
			]);
			if (sessionStorage.getItem("phoenix-isolation-reload") !== "1") {
				sessionStorage.setItem("phoenix-isolation-reload", "1");
				location.reload();
				return { reloading: true };
			}
		} catch {
			// A separately built ordinary runtime remains usable without isolation.
		}
	}
	if (supported()) sessionStorage.removeItem("phoenix-isolation-reload");
	return { reloading: false, script: (role) => runtimeScript(capabilities, supported(), role) };
}
