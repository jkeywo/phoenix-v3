// Keep the first tab's existing durable profile; concurrent tabs get their own.
// Locks, unlike heartbeat expiry, cannot mistake a throttled tab for a closed one.
export async function acquireSessionProfile(profile, { locks, storage, randomUUID }) {
	if (!locks) throw Error("Exclusive browser profile locks are unavailable.");
	const key = "phoenix-v3-session-profile:" + profile;
	let previous;
	try {
		previous = storage?.getItem(key);
	} catch {}
	if (
		previous !== profile &&
		!new RegExp("^" + profile.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + "-[a-f0-9]{32}$").test(previous || "")
	)
		previous = null;
	const claim = (candidate) =>
		new Promise((resolve, reject) => {
			locks
				.request("phoenix-v3-session-profile:" + candidate, { ifAvailable: true }, (lock) => {
					if (!lock) {
						resolve(null);
						return;
					}
					return new Promise((release) => resolve({ profile: candidate, release }));
				})
				.catch(reject);
		});
	let lease = previous ? await claim(previous) : null;
	if (!lease && previous !== profile) lease = await claim(profile);
	while (!lease) lease = await claim(profile + "-" + randomUUID().replaceAll("-", ""));
	try {
		storage?.setItem(key, lease.profile);
	} catch {}
	return lease;
}
