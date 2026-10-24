// Explicit local browser performance capture; never part of the automatic gate.
// Usage: node scripts/profile-web.cjs <playwright-package> <browser-exe> <url> <output-prefix> [--headed] [--no-cpu]
const fs = require("node:fs");
const { chromium } = require(process.argv[2]);
(async () => {
	const browser = await chromium.launch({
		executablePath: process.argv[3],
		headless: !process.argv.includes("--headed"),
		args: ["--enable-gpu", "--use-angle=d3d11", "--autoplay-policy=no-user-gesture-required"],
	});
	const page = await browser.newPage({ viewport: { width: 1920, height: 1080 }, deviceScaleFactor: 1 });
	try {
		const lines = [],
			errors = [];
		page.on("console", (msg) => {
			if (msg.text().startsWith("PHOENIX_")) {
				lines.push(msg.text());
				console.log(msg.text());
			} else if (msg.type() === "error") console.error(msg.text());
		});
		page.on("pageerror", (e) => {
			errors.push(String(e));
			console.error(String(e));
		});
		const cdp = await page.context().newCDPSession(page);
		const sampleCpu = !process.argv.includes("--no-cpu");
		if (sampleCpu) {
			await cdp.send("Profiler.enable");
			await cdp.send("Profiler.setSamplingInterval", { interval: 1000 });
		}
		await page.goto(process.argv[4], { timeout: 120000 });
		await page.waitForFunction(
			() => window.profileReports?.some((x) => x.startsWith("PHOENIX_READY")) || window.profileComplete,
			null,
			{ timeout: 120000 },
		);
		if (sampleCpu) await cdp.send("Profiler.start");
		let failure;
		try {
			const seconds = Number(new URL(process.argv[4]).searchParams.get("seconds") || 45);
			await page.waitForFunction(() => window.profileComplete, null, {
				timeout: Math.max(180000, (seconds + 120) * 1000),
			});
		} catch (error) {
			failure = error;
			errors.push(String(error));
		}
		const profile = sampleCpu ? (await cdp.send("Profiler.stop")).profile : undefined;
		const metadata = await page.evaluate(() => {
			const canvas = document.getElementById("canvas"),
				gl = canvas.getContext("webgl2"),
				debug = gl?.getExtension("WEBGL_debug_renderer_info");
			return {
				userAgent: navigator.userAgent,
				visible: !document.hidden,
				hardwareConcurrency: navigator.hardwareConcurrency,
				renderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : null,
				width: canvas.width,
				height: canvas.height,
			};
		});
		metadata.headless = !process.argv.includes("--headed");
		metadata.cpuSampling = sampleCpu;
		fs.writeFileSync(
			process.argv[5] + ".json",
			JSON.stringify(
				{
					metadata,
					lines: lines.map((x) => ({
						kind: x.split(" ")[0],
						report: JSON.parse(x.slice(x.indexOf(" ") + 1)),
					})),
					errors,
				},
				null,
				2,
			),
		);
		if (profile) fs.writeFileSync(process.argv[5] + ".cpuprofile", JSON.stringify(profile));
		await page.screenshot({ path: process.argv[5] + ".png", timeout: 30000 });
		console.log(JSON.stringify({ output: process.argv[5], metadata, reports: lines.length, errors }));
		if (failure) throw failure;
	} finally {
		await browser.close();
	}
})().catch((e) => {
	console.error(e);
	process.exit(1);
});
