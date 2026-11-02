// Explicit real-process check; the bounded headless feature test also runs without networking.
import { spawn } from "node:child_process";
import { resolve } from "node:path";
import { createSignallingServer } from "../../service/server.mjs";

const service = createSignallingServer({ port: 0 });
try {
	const address = await service.listen();
	const executable = resolve(process.argv[2] || "build/demo-native/Release/phoenix_feature_tests.exe");
	await new Promise((accept, reject) => {
		const child = spawn(executable, [`ws://127.0.0.1:${address.port}/phoenix`], { stdio: "inherit" });
		child.on("error", reject);
		child.on("exit", (code) => (code === 0 ? accept() : reject(Error(`Feature network check failed (${code})`))));
	});
} finally {
	await service.close();
}
