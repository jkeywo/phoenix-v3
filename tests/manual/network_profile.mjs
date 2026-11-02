// Explicit production-path traffic diagnostic, excluded from timing gates.
import { spawn } from "node:child_process";
import { createWriteStream } from "node:fs";
import { copyFile, mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { createInterface } from "node:readline";
import { createHash, randomUUID } from "node:crypto";
import { createTrafficObserver } from "./network_traffic.mjs";
const cli = process.argv.slice(2);
const separate = cli.includes("--multiprocess"),
	direct = cli.includes("--direct"),
	staggered = cli.includes("--staggered"),
	warmGM = cli.includes("--warm-gm"),
	startupProfile = cli.includes("--startup-profile"),
	cold = cli.includes("--cold");
for (let i = cli.length - 1; i >= 0; --i)
	if (["--multiprocess", "--direct", "--staggered", "--warm-gm", "--startup-profile", "--cold"].includes(cli[i]))
		cli.splice(i, 1);
const [executable, pack, scenario, slot, ship, clients, seconds, output, gm = "crew", drop = "0"] = cli;
if (!output)
	throw new Error(
		"Usage: network_profile.mjs [--multiprocess] [--direct] [--staggered] [--warm-gm] [--startup-profile] [--cold] executable pack scenario slot ship clients seconds output [gm] [dropEvery]",
	);
const count = Number(clients),
	capture = Number(seconds),
	dropEvery = Number(drop);
if (
	!Number.isSafeInteger(count) ||
	count < 1 ||
	count > 5 ||
	!Number.isSafeInteger(capture) ||
	capture < 10 ||
	capture > 600 ||
	!Number.isSafeInteger(dropEvery) ||
	dropEvery < 0
)
	throw new Error("Requires 1-5 clients, 10-600 seconds and a nonnegative integer drop interval");
if (direct && (!separate || dropEvery))
	throw new Error("--direct requires --multiprocess and cannot use relay-only packet loss injection");
if ((staggered || warmGM) && !separate)
	throw new Error("Staggered startup and warm GM cache require separate processes");
if (warmGM && gm !== "gm") throw new Error("--warm-gm requires a GM client");
if (cold) {
	for (const directory of [output, output + ".client"]) {
		const existing = await readdir(directory).catch((error) => {
			if (error.code === "ENOENT") return [];
			throw error;
		});
		if (existing.length) throw new Error("Cold capture requires unused application storage: " + directory);
	}
}
await mkdir(output, { recursive: true });
await rm(resolve(output, "measure.abort"), { force: true });
for (const name of await readdir(output))
	if (name.startsWith("transport-") && name.endsWith(".json")) await rm(resolve(output, name));
const snapshot = resolve(
	output,
	executable.toLowerCase().endsWith(".exe") ? "network-profile-probe.exe" : "network-profile-probe",
);
await copyFile(resolve(executable), snapshot);
const executableHash = createHash("sha256")
	.update(await readFile(snapshot))
	.digest("hex");
const observer = await createTrafficObserver(dropEvery);
const log = createWriteStream(resolve(output, "process.log"));
// At most seven bounded children share this log; pipe installs two listeners each.
log.setMaxListeners(32);
const childEnvironment = { ...process.env };
delete childEnvironment.PHOENIX_PROFILE_STARTUP;
if (startupProfile) childEnvironment.PHOENIX_PROFILE_STARTUP = "1";
for (const key of ["PHOENIX_PROFILE_DIRECT", "PHOENIX_PROFILE_WARM_GM", "PHOENIX_TRANSPORT_PROFILE_DIRECTORY"])
	delete childEnvironment[key];
if (separate) childEnvironment.PHOENIX_TRANSPORT_PROFILE_DIRECTORY = resolve(output);
if (direct) childEnvironment.PHOENIX_PROFILE_DIRECT = "1";
if (warmGM) childEnvironment.PHOENIX_PROFILE_WARM_GM = "1";
const children = [],
	clientStorageDirectories = [],
	exitCodes = {};
let sessionReport, trafficDuration, failure;
let startupOrigin;
const startupMilestones = [],
	startupWork = [];
function milestone(peer, event, details = {}) {
	startupMilestones.push({ peer, event, seconds: (performance.now() - startupOrigin) / 1000, ...details });
}
function launch(name, args, onLine) {
	startupOrigin ??= performance.now();
	milestone(name, "spawn");
	const child = spawn(snapshot, args, {
		windowsHide: true,
		env: childEnvironment,
		stdio: ["ignore", "pipe", "pipe"],
	});
	child.stdout.pipe(log, { end: false });
	child.stderr.pipe(log, { end: false });
	const lines = createInterface({ input: child.stdout });
	lines.on("line", (line) => {
		try {
			if (line.startsWith("PHOENIX_STARTUP_STAGE ")) {
				const row = JSON.parse(line.slice("PHOENIX_STARTUP_STAGE ".length));
				startupWork.push({
					peer: name,
					stage: row.stage,
					milliseconds: row.milliseconds,
					count: row.count,
					seconds: (performance.now() - startupOrigin) / 1000,
				});
			}
			if (line.startsWith("PHOENIX_PEER_HOST_READY ")) milestone(name, "host_ready");
			if (line.startsWith("PHOENIX_PEER_CLIENT_READY ")) milestone(name, "console_ready");
			if (line.startsWith("PHOENIX_PEER_MILESTONE ")) {
				const row = JSON.parse(line.slice("PHOENIX_PEER_MILESTONE ".length));
				milestone(name, row.stage, { process_seconds: row.seconds });
			}
			onLine(line);
		} catch (error) {
			failure = String(error.stack || error);
			killChildren();
		}
	});
	const done = new Promise((accept, reject) => {
		child.once("error", reject);
		child.once("close", (code, signal) => {
			exitCodes[name] = { code, signal };
			accept(code ?? 1);
		});
	});
	children.push({ child, done });
	return { child, done };
}
function killChildren() {
	for (const { child } of children) if (child.exitCode === null && !child.killed) child.kill();
}
const watchdog = setTimeout(
	() => {
		console.error("Network diagnostic startup/capture watchdog expired; stopping its own children.");
		killChildren();
	},
	(capture + 15 * 60) * 1000,
);
async function combined() {
	const process = launch(
		"combined",
		[resolve(pack), scenario, slot, ship, clients, seconds, resolve(output), observer.url, gm, "relay"],
		(line) => {
			if (line.startsWith("PHOENIX_NETWORK_READY ")) {
				observer.begin();
				console.log(line);
			}
			if (line.startsWith("PHOENIX_NETWORK_PROFILE ")) {
				const report = JSON.parse(line.slice("PHOENIX_NETWORK_PROFILE ".length));
				if (Number.isFinite(report.capture_seconds)) sessionReport = report;
			}
		},
	);
	const code = await process.done;
	trafficDuration = observer.stop();
	return code || (sessionReport ? 0 : 1);
}
async function multiprocess() {
	const startFile = resolve(output, "measure.start"),
		stopFile = resolve(output, "measure.stop");
	await rm(startFile, { force: true });
	await rm(stopFile, { force: true });
	const room = "profile-" + randomUUID(),
		expected = count + Number(gm === "gm"),
		ready = new Set(),
		reports = {};
	let prepared,
		resolvePrepared,
		rejectPrepared,
		rejectFailure,
		began = false,
		startWrite;
	const preparedGate = new Promise((accept, reject) => {
		resolvePrepared = accept;
		rejectPrepared = reject;
	});
	const failed = new Promise((accept, reject) => {
		rejectFailure = reject;
	});
	failed.catch(() => {});
	const watch = (process, name) => {
		process.done.then((code) => {
			if (code) rejectFailure(new Error(name + " exited with " + code));
		}, rejectFailure);
		return process;
	};
	const host = watch(
		launch(
			"host",
			["host", resolve(pack), scenario, slot, ship, clients, seconds, resolve(output), observer.url, room],
			(line) => {
				if (line.startsWith("PHOENIX_PEER_HOST_READY ")) {
					prepared = JSON.parse(line.slice("PHOENIX_PEER_HOST_READY ".length));
					resolvePrepared(prepared);
				}
				if (line.startsWith("PHOENIX_PEER_REPORT ")) {
					const report = JSON.parse(line.slice("PHOENIX_PEER_REPORT ".length));
					reports[report.peer] = report;
				}
			},
		),
		"host",
	);
	host.done.then(() => {
		if (!prepared) rejectPrepared(new Error("Host exited before preparing the scenario"));
	}, rejectPrepared);
	await Promise.race([preparedGate, failed]);
	const maybeBegin = () => {
		if (!began && ready.size === expected) {
			began = true;
			observer.begin();
			console.log(
				"PHOENIX_NETWORK_READY " +
					JSON.stringify({ clients: count, gm: gm === "gm", separate_processes: true }),
			);
			startWrite = Promise.all(
				[output, ...clientStorageDirectories].map((directory) =>
					writeFile(resolve(directory, "measure.start"), "start\n"),
				),
			);
			startWrite.catch(rejectFailure);
		}
	};
	const peers = [];
	for (let i = 0; i < expected; ++i) {
		const clientOutput = resolve(output, "client-" + i);
		await mkdir(clientOutput, { recursive: true });
		clientStorageDirectories.push(clientOutput);
		let resolveReady;
		const clientReady = new Promise((accept) => {
			resolveReady = accept;
		});
		const peer = watch(
			launch(
				"client-" + i,
				[
					"client",
					resolve(pack),
					i < count ? prepared.consoles[i] : "gm",
					String(i),
					seconds,
					clientOutput,
					observer.url,
					room,
					i < count ? "crew" : "gm",
				],
				(line) => {
					if (line.startsWith("PHOENIX_PEER_CLIENT_READY ")) {
						ready.add(JSON.parse(line.slice("PHOENIX_PEER_CLIENT_READY ".length)).index);
						resolveReady();
						maybeBegin();
					}
					if (line.startsWith("PHOENIX_PEER_CLIENT_WAIT "))
						ready.delete(JSON.parse(line.slice("PHOENIX_PEER_CLIENT_WAIT ".length)).index);
					if (line.startsWith("PHOENIX_PEER_REPORT ")) {
						const report = JSON.parse(line.slice("PHOENIX_PEER_REPORT ".length));
						reports[report.peer] = report;
					}
				},
			),
			"client-" + i,
		);
		peers.push(peer);
		if (staggered) await Promise.race([clientReady, failed]);
	}
	await Promise.race([Promise.all(peers.map((p) => p.done)), failed]);
	await startWrite;
	trafficDuration = observer.stop();
	await writeFile(stopFile, "stop\n");
	await Promise.race([host.done, failed]);
	const hostReport = reports["profile-host"];
	sessionReport = {
		schema: 1,
		scenario,
		ship,
		pack_hash: prepared.pack_hash,
		clients: count,
		gm: gm === "gm",
		force_relay: !direct,
		staggered_startup: staggered,
		gm_cache: warmGM ? "warm verified base archive" : "cold",
		separate_processes: true,
		capture_seconds: trafficDuration,
		captured_ticks: hostReport?.captured_ticks,
		host: hostReport,
		peers: Object.values(reports).filter((p) => p.role !== "host"),
	};
	await writeFile(resolve(output, "session.json"), JSON.stringify(sessionReport, null, 2) + "\n");
	return began && hostReport && sessionReport.peers.length === expected ? 0 : 1;
}
let code = 1;
try {
	code = separate ? await multiprocess() : await combined();
} catch (error) {
	failure ||= String(error.stack || error);
	console.error(failure);
} finally {
	clearTimeout(watchdog);
	if (trafficDuration === undefined) trafficDuration = observer.stop();
	if (separate && (code || failure)) {
		await Promise.all(
			[output, ...clientStorageDirectories].map((directory) =>
				writeFile(resolve(directory, "measure.abort"), "abort\n"),
			),
		);
		let grace;
		try {
			await Promise.race([
				Promise.allSettled(children.map((p) => p.done)),
				new Promise((done) => {
					grace = setTimeout(done, 2000);
				}),
			]);
		} finally {
			clearTimeout(grace);
		}
	}
	killChildren();
	await Promise.allSettled(children.map((p) => p.done));
	const nativeReports = await Promise.all(
		(await readdir(output))
			.filter((name) => name.startsWith("transport-") && name.endsWith(".json"))
			.map(async (name) => JSON.parse(await readFile(resolve(output, name), "utf8"))),
	).catch((error) => {
		code = 1;
		failure ||= "Cannot read native traffic counters: " + String(error);
		return [];
	});
	const expectedNative = separate ? count + 1 + Number(gm === "gm") : 0;
	if (!code && (nativeReports.length !== expectedNative || nativeReports.some((report) => report.incomplete))) {
		code = 1;
		failure ||= "Native traffic counters are missing or incomplete";
	}
	await writeFile(
		resolve(output, "traffic.json"),
		JSON.stringify(
			{
				schema: 2,
				executable_sha256: executableHash,
				exit_code: code,
				process_exits: exitCodes,
				native_transport_expected_reports: expectedNative,
				native_transport: nativeReports,
				failure,
				session: sessionReport,
				traffic_capture_seconds: trafficDuration,
				startup: {
					application_storage: cold ? "fresh per host/client, enforced empty" : "caller managed",
					definition:
						"Fresh host spawn through five continuous seconds of usable observations per Console; existing readiness gate is unchanged.",
					profiling_enabled: startupProfile,
					milestones: startupMilestones,
					work: startupWork,
				},
				...observer.summary(trafficDuration),
				units: "bytes unless labelled bits",
				scope: "Real production admission, host/client sessions and loopback signalling (preferred direct when --direct is selected). Native counters measure application messages without WebRTC framing. Counts sizes/types only. Application bytes include authentication/base64; relay bytes include JSON escaping. WebSocket headers are estimated; TLS/TCP/IP, ICE and direct WebRTC framing are excluded. The proxy-to-service hop is not counted twice.",
			},
			null,
			2,
		) + "\n",
	);
	await observer.close();
	log.end();
}
console.log("Network measurement complete: exit=" + code + "; report=" + resolve(output, "traffic.json"));
process.exitCode = code;
