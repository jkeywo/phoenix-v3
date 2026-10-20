import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
const root = path.resolve(process.argv[2] || "build/pages");
const excluded = new Set([
	"test.zip",
	"test-3d.zip",
	"training.zip",
	"test.zip.menu.json",
	"test-3d.zip.menu.json",
	"training.zip.menu.json",
	"alliance_courier_recreated.glb",
	"asteroid_common_1_lod1.glb",
	"phoenix_space_cubemap.png",
]);
async function inspect(folder) {
	for (const entry of await readdir(folder, { withFileTypes: true })) {
		const file = path.join(folder, entry.name);
		if (excluded.has(entry.name)) throw Error("Development content in deployment: " + file);
		if (entry.isDirectory()) await inspect(file);
		else if (entry.name.endsWith(".js")) {
			const source = await readFile(file, "utf8");
			if (/"filename"\s*:\s*"[^"\n]*\/(?:test|test-3d|training)\.zip"/.test(source))
				throw Error("Development content preloaded by " + file);
		}
	}
}
await inspect(root);
console.log("Deployment excludes development test bases and their model assets.");
