// Root content must pass the production C++ verifier before exposing its menu.
export function verifyLaunchBase(
	bytes,
	createWorker = () => new Worker(new URL("./base-verifier-worker.js", import.meta.url)),
) {
	return new Promise((resolve, reject) => {
		const worker = createWorker();
		const finish = (error, preview) => {
			worker.terminate();
			if (error) reject(new Error(error));
			else resolve(preview);
		};
		worker.onerror = (event) => finish(event.message || "Launch base verification failed.");
		worker.onmessage = ({ data }) => finish(data.ok ? null : data.error, data.preview);
		// The launcher retains its own verified bytes for role launches and archive storage.
		worker.postMessage(bytes);
	});
}
