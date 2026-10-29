// Synthetic asynchronous byte storage for the compiled browser adapter. This
// exercises C++ encoding, JS transaction handling and callback correlation;
// actual IndexedDB durability/reload remains a browser smoke check.
const databases = new Map();
globalThis.indexedDB = {
	open(name) {
		const opening = {};
		queueMicrotask(() => {
			const fresh = !databases.has(name);
			if (fresh) databases.set(name, new Map());
			const records = databases.get(name);
			opening.result = {
				createObjectStore() {},
				close() {},
				transaction(_store, _mode, options) {
					let aborted = false;
					const transaction = {
						durability: options?.durability,
						abort() {
							aborted = true;
							queueMicrotask(() => transaction.onabort?.());
						},
						objectStore() {
							const operation = (result) => {
								const request = { result };
								queueMicrotask(() => {
									request.onsuccess?.();
									if (!aborted) transaction.oncomplete?.();
								});
								return request;
							};
							return {
								put(bytes, key) {
									records.set(key, bytes.slice());
									return operation(key);
								},
								get(key) {
									return operation(records.get(key)?.slice());
								},
							};
						},
					};
					return transaction;
				},
			};
			if (fresh) opening.onupgradeneeded?.();
			opening.onsuccess?.();
		});
		return opening;
	},
};
// A stalled production callback must fail rather than keep CI alive indefinitely.
setTimeout(() => {
	console.error("Browser blob contract test timed out");
	process.exit(1);
}, 30000).unref();
