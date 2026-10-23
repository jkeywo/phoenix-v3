// Gameplay entry requires production verification; display metadata can open the menu first.
export function verifyLaunchBase(
	bytes,
	createWorker = () => new Worker(new URL("./base-verifier-worker.js", import.meta.url)),
	onProgress = () => {},
) {
	return new Promise((resolve, reject) => {
		const worker = createWorker();
		const finish = (error, preview) => {
			worker.terminate();
			if (error) reject(new Error(error));
			else resolve(preview);
		};
		worker.onerror = (event) => finish(event.message || "Launch base verification failed.");
		worker.onmessage = ({ data }) => {
			if (data.phase) onProgress(data);
			else finish(data.ok ? null : data.error, data.preview);
		};
		// The launcher retains its own verified bytes for role launches and archive storage.
		worker.postMessage(bytes);
	});
}
