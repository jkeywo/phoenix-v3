import { createHash, generateKeyPairSync, createPrivateKey, createPublicKey, sign, verify } from "node:crypto";

// A room is a fingerprint of its issuer key. Hosts hold the issuer capability;
// stations receive only a signed identity. This survives a stateless relay restart
// without giving a station the ability to issue host credentials in that room.
const fingerprint = (key) => createHash("sha256").update(key).digest("hex").slice(0, 24);
export function createIssuer() {
	const pair = generateKeyPairSync("ed25519");
	const publicKey = pair.publicKey.export({ type: "spki", format: "der" }).toString("base64url");
	const privateKey = pair.privateKey.export({ type: "pkcs8", format: "der" }).toString("base64url");
	return { room: fingerprint(publicKey), publicKey, privateKey };
}
export function issueIdentity(issuer, claims) {
	const body = Buffer.from(
		JSON.stringify({
			...claims,
			publicKey: issuer.publicKey,
			...(claims.role === "host" ? { issuer: issuer.privateKey } : {}),
		}),
	).toString("base64url");
	const key = createPrivateKey({ key: Buffer.from(issuer.privateKey, "base64url"), format: "der", type: "pkcs8" });
	return body + "." + sign(null, Buffer.from(body), key).toString("base64url");
}
export function readIdentity(credential) {
	if (typeof credential !== "string" || credential.length > 4096) throw Error("Invalid identity");
	const parts = credential.split(".");
	if (parts.length !== 2) throw Error("Invalid identity");
	const [body, signature] = parts,
		claims = JSON.parse(Buffer.from(body, "base64url"));
	if (typeof claims.publicKey !== "string" || fingerprint(claims.publicKey) !== claims.room)
		throw Error("Wrong room issuer");
	const key = createPublicKey({ key: Buffer.from(claims.publicKey, "base64url"), format: "der", type: "spki" });
	if (
		key.asymmetricKeyType !== "ed25519" ||
		!verify(null, Buffer.from(body), key, Buffer.from(signature, "base64url"))
	)
		throw Error("Invalid signature");
	if (claims.role === "host") {
		const issuer = createPrivateKey({ key: Buffer.from(claims.issuer, "base64url"), format: "der", type: "pkcs8" });
		if (createPublicKey(issuer).export({ type: "spki", format: "der" }).toString("base64url") !== claims.publicKey)
			throw Error("Invalid issuer capability");
		if (
			!Array.isArray(claims.roster) ||
			claims.roster.length < 1 ||
			claims.roster.length > 3 ||
			new Set(claims.roster).size !== claims.roster.length ||
			claims.roster.some((p) => !Number.isInteger(p) || p < 1 || p > 99) ||
			!claims.roster.includes(claims.peer)
		)
			throw Error("Invalid host roster");
	} else if (
		claims.role !== "station" ||
		claims.issuer ||
		!Number.isInteger(claims.peer) ||
		claims.peer < 100 ||
		claims.peer > 0x7fffffff
	)
		throw Error("Invalid station");
	if (!Number.isSafeInteger(claims.expires) || claims.expires <= Date.now() || claims.expires > Date.now() + 86400000)
		throw Error("Expired identity");
	return claims;
}
