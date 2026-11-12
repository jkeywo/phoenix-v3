// Install the pinned release artifact while building the service image.
import { createReadStream, createWriteStream, existsSync } from "node:fs";
import { readFile, mkdir, rename, writeFile, readdir, unlink } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
import { Readable, Transform } from "node:stream";
import { pipeline } from "node:stream/promises";
import { execFileSync } from "node:child_process";
import { resolve } from "node:path";
import { createBrotliDecompress } from "node:zlib";

export async function installAssets(directory = fileURLToPath(new URL(".", import.meta.url))) {
	if (process.env.PHOENIX_SKIP_ARTIFACTS === "1") return;
	const descriptor = resolve(directory, "artifact.json");
	if (!existsSync(descriptor)) throw Error("Missing pinned V4 artifact descriptor");
	const manifest = JSON.parse(await readFile(descriptor, "utf8"));
	if (
		!/^https:\/\/github\.com\/jkeywo\/phoenix-v3\/releases\/download\//.test(manifest.url) ||
		!/^[a-f0-9]{64}$/.test(manifest.sha256)
	)
		throw Error("Invalid artifact identity");
	const marker = resolve(directory, "public/.artifact-sha256");
	if (existsSync(marker) && (await readFile(marker, "utf8")).trim() === manifest.sha256) return;
	const archive = resolve(directory, "download.pending");
	const response = await fetch(manifest.url, { signal: AbortSignal.timeout(300000) });
	if (!response.ok) throw Error(`V4 artifact download failed: ${response.status}`);
	let bytes = 0;
	const hash = createHash("sha256");
	await pipeline(
		Readable.fromWeb(response.body),
		new Transform({
			transform(chunk, encoding, next) {
				bytes += chunk.length;
				if (bytes > manifest.bytes || bytes > 1024 ** 3) {
					next(Error("Artifact exceeds recorded size"));
					return;
				}
				hash.update(chunk);
				next(null, chunk);
			},
		}),
		createWriteStream(archive),
	);
	if (bytes !== manifest.bytes || hash.digest("hex") !== manifest.sha256)
		throw Error("V4 artifact size/hash mismatch");
	const verified = resolve(directory, "download.tar.gz");
	await rename(archive, verified);
	// Reject archive paths before extraction, even though the complete archive is pinned.
	const names = execFileSync("tar", ["-tzf", verified], { encoding: "utf8" }).trim().split("\n");
	if (
		names.some(
			(n) => n.startsWith("/") || n.includes("\\") || n.split("/").includes("..") || !n.startsWith("public/"),
		)
	)
		throw Error("Unsafe artifact path");
	await mkdir(resolve(directory, "public"), { recursive: true });
	execFileSync("tar", ["-xzf", verified, "-C", directory], { stdio: "inherit" });
	if (manifest.compressedOnly) {
		const publicRoot = resolve(directory, "public");
		for (const version of await readdir(resolve(publicRoot, "versions"))) {
			const files = JSON.parse(
				await readFile(resolve(publicRoot, "versions", version, "artifacts.json"), "utf8"),
			).files;
			for (const file of files) {
				const path = resolve(publicRoot, file.path);
				if (!path.startsWith(publicRoot + "/") && !path.startsWith(publicRoot + "\\"))
					throw Error("Invalid asset path");
				if (!existsSync(path) && existsSync(path + ".br"))
					await pipeline(createReadStream(path + ".br"), createBrotliDecompress(), createWriteStream(path));
				const digest = createHash("sha256");
				let size = 0;
				for await (const chunk of createReadStream(path)) {
					digest.update(chunk);
					size += chunk.length;
				}
				if (digest.digest("hex") !== file.sha256 || size !== file.bytes)
					throw Error("Installed asset hash mismatch: " + file.path);
			}
		}
	}
	await unlink(verified);
	await writeFile(marker, manifest.sha256 + "\n");
	console.log(`Installed Phoenix V4 browser artifact ${manifest.build}`);
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await installAssets();
