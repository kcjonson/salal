#!/usr/bin/env node
// The perf suite's runner (docs/testing/README.md, Performance). Node, no dependencies.
//
//   node scripts/perf.mjs run [options]       run this checkout's perf scenes, write perf-results/local/
//   node scripts/perf.mjs compare [options]   run a base build and this one interleaved, fail past the threshold
//   node scripts/perf.mjs noise [options]     run one build many times and report its run-to-run spread
//
// Options:
//   --exe <path>          a sandbox binary to run instead of building this checkout
//   --base <ref>          compare: the git ref to build in a scratch worktree outside the repo (default main)
//   --base-exe <path>     compare: a base sandbox binary instead of --base
//   --scenes <a,b>        the scenes (default every perf_ scene)
//   --runs <n>            launches per scene and build (default 5 for run and compare, 10 for noise)
//   --warmup <seconds>    before the capture (default 3)
//   --window <seconds>    the capture (default 5)
//   --port <port>         the dev server's port (default 8094; never 8090, which other sessions use)
//   --threshold <pct>     compare: the regression that fails it (default THRESHOLD_PCT below)
//   --out <path>          where the JSON goes (run: perf-results/local/<describe>.json)
//   --record              run: write perf-results/<machine>/<describe>.json, the file a tag's result is
//                         committed as; refused for --exe or uncommitted changes
//   --wait-busy <seconds> how long to wait for another build (cl, link, ninja) to finish before a run (default 900)
//   --affinity <mask>     the logical CPUs the sandbox may run on, a hex mask (default: defaultAffinity());
//                         none to leave it to the scheduler
//
// The sandbox is built Release with SALAL_DEV_SERVER_IN_RELEASE in build-perf/, beside build/, so a perf
// run measures what ships and leaves the developer's build alone. Each run launches the sandbox with
// --scene, --http-port and --no-vsync, waits for /api/health, warms up, captures a fixed window through
// /api/control?action=capture, reads /api/metrics, and exits through /api/control?action=exit.

import { execFileSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SCENES = ['perf_quads', 'perf_text', 'perf_world'];
const FORBIDDEN_PORT = 8090;

// Gated statistics: the median over a build's runs of each run's value. A head median more than the
// threshold above the base median fails compare. Set from measured noise; docs/testing/README.md has
// the numbers. GPU time is gated in perf_quads only: perf_text's is 70 us, where a few microseconds
// are 3%, and perf_world's present waits or doesn't launch to launch, and the GPU's clocks follow.
const THRESHOLD_PCT = 4;
const GATED_EVERYWHERE = ['cpuMs.p50', 'renderMs.p50'];
const GATED = { perf_quads: [...GATED_EVERYWHERE, 'gpuMs.p50'] };
const gatedIn = (scene) => GATED[scene] ?? GATED_EVERYWHERE;
// Reported beside them, not gated: tails and counts move with intent (the rework changes the counts).
const REPORTED = [
	'gpuMs.p50',
	'frameMs.p50',
	'frameMs.p99',
	'presentMs.p50',
	'cpuMs.p99',
	'renderMs.p99',
	'flushMs.p50',
	'gpuMs.p99',
	'drawCalls.p50',
	'vertices.p50',
	'instances.p50',
	'uploadBytes.p50',
];
const KEYS = [...new Set([...GATED_EVERYWHERE, ...REPORTED])];
const FLAGS = ['record'];
const BUSY = ['cl.exe', 'link.exe', 'ninja.exe', 'cl', 'ld', 'ninja'];

function parseArgs(argv) {
	const [mode = 'run', ...rest] = argv;
	const options = {};
	for (let i = 0; i < rest.length; ++i) {
		const arg = rest[i];
		if (!arg.startsWith('--')) {
			fail(`unexpected argument ${arg}`);
		}
		const [key, inline] = arg.slice(2).split('=', 2);
		options[key] = FLAGS.includes(key) ? true : (inline ?? rest[++i]);
	}
	return { mode, options };
}

function fail(message) {
	console.error(`perf: ${message}`);
	process.exit(2);
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function run(command, args, cwd) {
	return execFileSync(command, args, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
}

// --- Builds ---

function exeIn(buildDir) {
	const windows = process.platform === 'win32';
	return windows ? path.join(buildDir, 'apps/sandbox/Release/sandbox.exe') : path.join(buildDir, 'apps/sandbox/sandbox');
}

function build(sourceDir) {
	const buildDir = path.join(sourceDir, 'build-perf');
	if (!fs.existsSync(path.join(buildDir, 'CMakeCache.txt'))) {
		const preset = process.platform === 'win32' ? ['--preset', 'windows'] : ['--preset', 'default', '-DCMAKE_BUILD_TYPE=Release'];
		console.log(`perf: configuring ${buildDir}`);
		execFileSync('cmake', [...preset, '-B', buildDir, '-DSALAL_DEV_SERVER_IN_RELEASE=ON', '-DSALAL_BUILD_TESTS=OFF'], {
			cwd: sourceDir,
			stdio: 'inherit',
		});
	}
	console.log(`perf: building the sandbox in ${buildDir}`);
	execFileSync('cmake', ['--build', buildDir, '--config', 'Release', '--target', 'salal_sandbox'], { cwd: sourceDir, stdio: 'inherit' });
	return exeIn(buildDir);
}

// A scratch worktree of `ref` outside the repo, reused across runs so its build is incremental.
function baseCheckout(ref) {
	const dir = path.join(os.tmpdir(), 'salal-perf', ref.replace(/[^A-Za-z0-9._-]/g, '_'));
	const commit = run('git', ['rev-parse', '--verify', `${ref}^{commit}`], ROOT);
	if (fs.existsSync(path.join(dir, '.git'))) {
		run('git', ['checkout', '--detach', '--force', commit], dir);
	} else {
		fs.mkdirSync(path.dirname(dir), { recursive: true });
		run('git', ['worktree', 'add', '--detach', dir, commit], ROOT);
	}
	return dir;
}

function describe(dir) {
	const commit = run('git', ['rev-parse', 'HEAD'], dir);
	let name = commit.slice(0, 12);
	try {
		name = run('git', ['describe', '--tags', '--always'], dir);
	} catch {}
	const dirty = run('git', ['status', '--porcelain', '--untracked-files=no'], dir) !== '';
	return { commit, describe: dirty ? `${name}-dirty` : name };
}

// --- The machine ---

function gpuName() {
	try {
		if (process.platform === 'win32') {
			return run('powershell', ['-NoProfile', '-Command', '(Get-CimInstance Win32_VideoController).Name -join "; "']);
		}
		if (process.platform === 'darwin') {
			const out = run('system_profiler', ['SPDisplaysDataType']);
			return out.match(/Chipset Model: (.*)/)?.[1] ?? null;
		}
		return run('sh', ['-c', 'lspci | grep -i -E "vga|3d" | cut -d: -f3']) || null;
	} catch {
		return null;
	}
}

function machine() {
	return {
		name: os.hostname().toLowerCase(),
		cpu: os.cpus()[0]?.model.trim() ?? null,
		threads: os.cpus().length,
		memoryGB: Math.round(os.totalmem() / 2 ** 30),
		gpu: gpuName(),
		os: `${process.platform} ${os.release()}`,
	};
}

function busyProcesses() {
	try {
		const names =
			process.platform === 'win32'
				? run('tasklist', ['/FO', 'CSV', '/NH'])
						.split(/\r?\n/)
						.map((line) => line.split('","')[0].replace(/^"/, '').toLowerCase())
				: run('ps', ['-A', '-o', 'comm=']).split('\n').map((line) => path.basename(line.trim()).toLowerCase());
		return [...new Set(names.filter((name) => BUSY.includes(name)))];
	} catch {
		return [];
	}
}

// Another session's build steals the cores a run measures: wait for it, or note it in the run.
async function waitForQuiet(seconds) {
	const deadline = Date.now() + seconds * 1000;
	let busy = busyProcesses();
	if (busy.length > 0) {
		console.log(`perf: waiting for ${busy.join(', ')} to finish`);
	}
	while (busy.length > 0 && Date.now() < deadline) {
		await sleep(5000);
		busy = busyProcesses();
	}
	return busy;
}

// --- One launch ---

function portFree(port) {
	return new Promise((resolve) => {
		const server = net.createServer();
		server.once('error', () => resolve(false));
		server.listen(port, '127.0.0.1', () => server.close(() => resolve(true)));
	});
}

async function get(port, route, timeoutMs = 30000) {
	const response = await fetch(`http://127.0.0.1:${port}${route}`, { signal: AbortSignal.timeout(timeoutMs) });
	const body = await response.text();
	if (!response.ok) {
		throw new Error(`${route}: ${response.status} ${body}`);
	}
	return JSON.parse(body);
}

async function launch(exe, scene, settings) {
	const { port } = settings;
	if (!(await portFree(port))) {
		throw new Error(`port ${port} is busy; pass --port`);
	}
	const busy = await waitForQuiet(settings.waitBusy);
	const args = [`--scene=${scene}`, '--http-port', String(port), '--no-vsync'];
	const pinned = settings.affinity !== null && process.platform === 'linux';
	const child = spawn(pinned ? 'taskset' : exe, pinned ? [settings.affinity, exe, ...args] : args, {
		cwd: path.dirname(exe),
		stdio: ['ignore', 'ignore', 'pipe'],
	});
	if (settings.affinity !== null && process.platform === 'win32') {
		run('powershell', ['-NoProfile', '-Command', `(Get-Process -Id ${child.pid}).ProcessorAffinity = ${settings.affinity}`]);
	}
	let stderr = '';
	child.stderr.on('data', (chunk) => (stderr = (stderr + chunk).slice(-4000)));
	const exited = new Promise((resolve) => child.once('exit', resolve));
	let gone = false;
	exited.then(() => (gone = true));

	try {
		const deadline = Date.now() + 30000;
		for (;;) {
			if (gone) {
				throw new Error(`the sandbox exited before serving (does this build have ${scene}?)\n${stderr}`);
			}
			try {
				await get(port, '/api/health', 1000);
				break;
			} catch {
				if (Date.now() > deadline) {
					throw new Error('the sandbox never answered /api/health');
				}
				await sleep(100);
			}
		}
		await sleep(settings.warmup * 1000);
		await get(port, '/api/control?action=capture&op=start');
		await sleep(settings.window * 1000);
		const { capture } = await get(port, '/api/control?action=capture&op=stop');
		const live = await get(port, '/api/metrics');
		capture.live = { drawCalls: live.drawCalls, vertexCount: live.vertexCount, uploadBytes: live.uploadBytes, flushCount: live.flushCount };
		capture.busy = busy;
		await get(port, '/api/control?action=exit');
		return capture;
	} finally {
		const timeout = sleep(15000).then(() => 'timeout');
		if ((await Promise.race([exited, timeout])) === 'timeout' || !gone) {
			child.kill();
		}
	}
}

// --- Statistics ---

const pick = (summary, key) => key.split('.').reduce((value, part) => value?.[part], summary);

function median(values) {
	const sorted = values.filter((v) => typeof v === 'number').sort((a, b) => a - b);
	if (sorted.length === 0) {
		return null;
	}
	const mid = Math.floor(sorted.length / 2);
	return sorted.length % 2 === 1 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
}

function summarise(runs) {
	const out = {};
	for (const key of KEYS) {
		out[key] = median(runs.map((r) => pick(r, key)));
	}
	return out;
}

function spread(runs) {
	const out = {};
	for (const key of KEYS) {
		const values = runs.map((r) => pick(r, key)).filter((v) => typeof v === 'number');
		const mid = median(values);
		if (!mid) {
			continue;
		}
		const mean = values.reduce((a, b) => a + b, 0) / values.length;
		const sd = Math.sqrt(values.reduce((a, b) => a + (b - mean) ** 2, 0) / Math.max(1, values.length - 1));
		out[key] = {
			median: mid,
			min: Math.min(...values),
			max: Math.max(...values),
			rangePct: (100 * (Math.max(...values) - Math.min(...values))) / mid,
			cvPct: (100 * sd) / mean,
		};
	}
	return out;
}

// The worst gap between the medians of two disjoint groups of `size` runs, over every way of taking
// them from these runs in order: what an A/A compare of `size` runs a side could show by noise alone.
function aaGap(values, size) {
	let worst = 0;
	for (let a = 0; a + size <= values.length; a += size) {
		for (let b = a + size; b + size <= values.length; b += size) {
			const left = median(values.slice(a, a + size));
			const right = median(values.slice(b, b + size));
			worst = Math.max(worst, (100 * Math.abs(right - left)) / Math.min(left, right));
		}
	}
	return worst;
}

// --- Modes ---

// Pinned to a fixed set of cores, a launch can't land on a different core, CCD or boost bin from the
// last, which on a 5950X moved a run's CPU time by up to a third. Logical CPUs 4 to 11 where there
// are 16 or more (clear of CPU 0's interrupts, inside the first CCD or the performance cores); none
// on a smaller machine or on macOS, which has no affinity.
function defaultAffinity() {
	const threads = os.cpus().length;
	return threads >= 16 && process.platform !== 'darwin' ? '0x0FF0' : null;
}

function settingsFrom(options, defaultRuns) {
	const port = Number(options.port ?? 8094);
	if (port === FORBIDDEN_PORT) {
		fail('port 8090 is the sandbox default other sessions use; pick another');
	}
	const scenes = options.scenes ? options.scenes.split(',') : SCENES;
	return {
		port,
		scenes,
		runs: Number(options.runs ?? defaultRuns),
		warmup: Number(options.warmup ?? 3),
		window: Number(options.window ?? 5),
		waitBusy: Number(options['wait-busy'] ?? 900),
		affinity: options.affinity === 'none' ? null : (options.affinity ?? defaultAffinity()),
	};
}

function header(settings) {
	return {
		schema: 1,
		date: new Date().toISOString(),
		machine: machine(),
		settings: {
			warmupSeconds: settings.warmup,
			windowSeconds: settings.window,
			runs: settings.runs,
			vsync: false,
			affinity: settings.affinity,
		},
		build: 'Release, SALAL_DEV_SERVER_IN_RELEASE=ON',
	};
}

function write(file, result) {
	fs.mkdirSync(path.dirname(file), { recursive: true });
	fs.writeFileSync(file, `${JSON.stringify(result, null, '\t')}\n`);
	console.log(`perf: wrote ${path.relative(process.cwd(), file)}`);
}

const fmt = (v) => (v === null || v === undefined ? '-' : Math.abs(v) >= 1000 ? v.toFixed(0) : v.toFixed(3));

async function runMode(options) {
	const settings = settingsFrom(options, 5);
	const record = 'record' in options;
	if (record && options.exe) {
		fail('--record measures this checkout, so it takes no --exe');
	}
	const source = options.exe ? null : describe(ROOT);
	if (record && source.describe.endsWith('-dirty')) {
		fail('--record needs a clean checkout, so the file names the commit it measured');
	}
	const exe = options.exe ? path.resolve(options.exe) : build(ROOT);
	const result = { ...header(settings), source, scenes: {} };
	for (const scene of settings.scenes) {
		const runs = [];
		for (let r = 0; r < settings.runs; ++r) {
			console.log(`perf: ${scene} run ${r + 1}/${settings.runs}`);
			runs.push(await launch(exe, scene, settings));
		}
		result.scenes[scene] = { median: summarise(runs), runs };
		console.log(`  ${gatedIn(scene).map((k) => `${k} ${fmt(result.scenes[scene].median[k])}`).join('  ')}`);
	}
	const folder = record ? path.join('perf-results', machine().name) : 'perf-results/local';
	write(options.out ?? path.join(ROOT, folder, `${source?.describe ?? 'exe'}.json`), result);
}

async function noiseMode(options) {
	const settings = settingsFrom(options, 10);
	const exe = options.exe ? path.resolve(options.exe) : build(ROOT);
	const result = { ...header(settings), source: options.exe ? null : describe(ROOT), scenes: {} };
	for (const scene of settings.scenes) {
		const runs = [];
		for (let r = 0; r < settings.runs; ++r) {
			console.log(`perf: ${scene} run ${r + 1}/${settings.runs}`);
			runs.push(await launch(exe, scene, settings));
		}
		const stats = spread(runs);
		for (const key of [...GATED_EVERYWHERE, 'gpuMs.p50']) {
			const values = runs.map((run) => pick(run, key));
			if (stats[key]) {
				stats[key].aaGap5Pct = aaGap(values, 5);
				stats[key].aaGap3Pct = aaGap(values, 3);
			}
		}
		result.scenes[scene] = { spread: stats, runs };
		console.log(`  ${scene}`);
		for (const [key, s] of Object.entries(stats)) {
			const gaps = s.aaGap5Pct === undefined ? '' : `  A/A gap 3: ${s.aaGap3Pct.toFixed(1)}%  5: ${s.aaGap5Pct.toFixed(1)}%`;
			console.log(`    ${key.padEnd(16)} median ${fmt(s.median)}  range ${s.rangePct.toFixed(1)}%  cv ${s.cvPct.toFixed(1)}%${gaps}`);
		}
	}
	write(options.out ?? path.join(ROOT, 'perf-results/local', `noise-${Date.now()}.json`), result);
}

async function compareMode(options) {
	const settings = settingsFrom(options, 5);
	const threshold = Number(options.threshold ?? THRESHOLD_PCT);
	const headExe = options.exe ? path.resolve(options.exe) : build(ROOT);
	let baseExe = options['base-exe'] ? path.resolve(options['base-exe']) : null;
	let baseSource = null;
	if (!baseExe) {
		const ref = options.base ?? 'main';
		const dir = baseCheckout(ref);
		baseExe = build(dir);
		baseSource = { ref, ...describe(dir) };
	}
	const result = {
		...header(settings),
		threshold,
		base: { exe: baseExe, source: baseSource },
		head: { exe: headExe, source: options.exe ? null : describe(ROOT) },
		scenes: {},
	};

	let failed = false;
	for (const scene of settings.scenes) {
		const base = [];
		const head = [];
		// Interleaved, alternating which goes first, so drift over the session lands on both.
		for (let r = 0; r < settings.runs; ++r) {
			console.log(`perf: ${scene} run ${r + 1}/${settings.runs}`);
			const order = r % 2 === 0 ? [base, head] : [head, base];
			for (const runs of order) {
				runs.push(await launch(runs === base ? baseExe : headExe, scene, settings));
			}
		}
		const b = summarise(base);
		const h = summarise(head);
		const rows = {};
		console.log(`  ${scene}`);
		for (const key of KEYS) {
			const deltaPct = b[key] ? (100 * (h[key] - b[key])) / b[key] : null;
			const gated = gatedIn(scene).includes(key);
			const regressed = gated && deltaPct !== null && deltaPct > threshold;
			failed ||= regressed;
			rows[key] = { base: b[key], head: h[key], deltaPct, gated, regressed };
			const delta = deltaPct === null ? '-' : `${deltaPct >= 0 ? '+' : ''}${deltaPct.toFixed(1)}%`;
			console.log(`    ${key.padEnd(16)} ${fmt(b[key]).padStart(12)} ${fmt(h[key]).padStart(12)} ${delta.padStart(8)}${regressed ? '  REGRESSED' : ''}`);
		}
		result.scenes[scene] = { metrics: rows, base, head };
	}
	result.failed = failed;
	write(options.out ?? path.join(ROOT, 'perf-results/local', `compare-${Date.now()}.json`), result);
	console.log(failed ? `perf: regressed past ${threshold}%` : `perf: within ${threshold}%`);
	process.exit(failed ? 1 : 0);
}

const { mode, options } = parseArgs(process.argv.slice(2));
const modes = { run: runMode, compare: compareMode, noise: noiseMode };
if (!modes[mode]) {
	fail(`unknown mode ${mode}; run, compare or noise`);
}
modes[mode](options).catch((error) => fail(error.stack ?? String(error)));
