// Only scheduling lives here. Authoritative state stays in the C++ session.
let pending = false;
self.onmessage = ({ data }) => {
	if (data !== "next" || pending) return;
	pending = true;
	setTimeout(() => {
		pending = false;
		self.postMessage("tick");
	}, 20);
};
