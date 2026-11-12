// Mount Phoenix V4 on V3's HTTP server without opening another listening port.
import { createService } from "./server.mjs";
import { fileURLToPath } from "node:url";

export function createV4Mount({
	host,
	root = fileURLToPath(new URL("./public", import.meta.url)),
	token = process.env.PHOENIX_V4_HOST_KEY || "",
} = {}) {
	const app = createService({ host, root, token, basePath: "/v4" });
	return {
		request(req, res) {
			if (req.url === "/v4") {
				res.writeHead(308, { Location: "/v4/" }).end();
				return true;
			}
			if (!req.url?.startsWith("/v4/")) return false;
			req.url = req.url.slice(3);
			app.server.emit("request", req, res);
			return true;
		},
		upgrade(req, socket, head) {
			if (!req.url?.startsWith("/v4/")) return false;
			req.url = req.url.slice(3);
			app.server.emit("upgrade", req, socket, head);
			return true;
		},
		close: app.close,
	};
}
