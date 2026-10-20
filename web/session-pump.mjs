// Session work must continue when the browser suspends animation frames.
// Acknowledgement keeps a busy/suspended page from accumulating worker messages.
export function startSessionPump(
	pump,
	createWorker = () => new Worker(new URL("./session-clock.js", import.meta.url)),
) {
	const worker = createWorker();
	let stopped = false;
	const stop = () => {
		stopped = true;
		worker.terminate();
	};
	worker.onmessage = () => {
		if (stopped) return;
		try {
			pump();
		} catch (error) {
			stop();
			throw error;
		}
		if (!stopped) worker.postMessage("next");
	};
	worker.postMessage("next");
	return stop;
}
