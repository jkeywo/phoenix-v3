// Reconstruct assets from the compressed, versioned files in this service commit.
import { createReadStream, createWriteStream, existsSync } from "node:fs";
import { readFile, readdir, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { resolve, sep } from "node:path";
import { createHash } from "node:crypto";
import { createBrotliDecompress } from "node:zlib";
import { pipeline } from "node:stream/promises";
import { reconstructWasm } from "./wasm-reconstruct.mjs";
const root = fileURLToPath(new URL("./public", import.meta.url));
if (process.env.PHOENIX_SKIP_ARTIFACTS !== "1") {
	const current = JSON.parse(await readFile(resolve(root, "current.json"), "utf8"));
	for (const version of await readdir(resolve(root, "versions"))) {
		const manifest = JSON.parse(await readFile(resolve(root, "versions", version, "artifacts.json"), "utf8"));
		if (manifest.id !== version) throw Error("Version directory identity mismatch");
		for (const asset of manifest.files) {
			const path = resolve(root, asset.path);
			if (!path.startsWith(root + sep)) throw Error("Asset path outside public root");
			if (
				!existsSync(path) &&
				(path.endsWith(".wasm") || path.endsWith(".pak")) &&
				existsSync(path + ".recipe.json.br")
			)
				await reconstructWasm(path, root);
			if (!existsSync(path) && existsSync(path + ".br"))
				await pipeline(createReadStream(path + ".br"), createBrotliDecompress(), createWriteStream(path));
			const hash = createHash("sha256");
			let bytes = 0;
			for await (const chunk of createReadStream(path)) {
				hash.update(chunk);
				bytes += chunk.length;
			}
			if (bytes !== asset.bytes || hash.digest("hex") !== asset.sha256)
				throw Error("Asset hash mismatch: " + asset.path);
		}
	}
	const catalogPath = resolve(root, "mods/example/catalog.json");
	if (existsSync(catalogPath)) {
		const catalog = JSON.parse(await readFile(catalogPath, "utf8"));
		for (const roles of Object.values(catalog.platforms))
			for (const artifact of Object.values(roles)) {
				for (const kind of ["pak", "manifest"]) {
					const filename = artifact[kind + "Url"];
					if (!/^[a-f0-9]{64}\.(pak|json)$/.test(filename)) throw Error("Invalid mod artifact path");
					const data = await readFile(resolve(root, "mods/example", filename));
					if (createHash("sha256").update(data).digest("hex") !== artifact[kind + "Sha256"])
						throw Error("Mod artifact hash mismatch");
				}
			}
	}
	await writeFile(resolve(root, ".verified-build"), current.id + "\n");
	console.log("Verified Phoenix V4 browser build " + current.id);
}
