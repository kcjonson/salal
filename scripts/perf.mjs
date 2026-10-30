#!/usr/bin/env node
// The perf suite's runner (docs/testing/README.md, Performance). Node, no dependencies.
//
//   node scripts/perf.mjs run [options]       run this checkout's perf scenes, write perf-results/local/
//   node scripts/perf.mjs compare [options]   run a base build and this one interleaved, gate the difference
//   node scripts/perf.mjs noise [options]     run one build many times: its spread, and the gate's error rates
//
// Options:
//   --exe <path>          a sandbox binary to run instead of building this checkout
//   --base <ref>          compare: the commit to build in a scratch worktree outside the repo (default: the
//                         merge-base of HEAD and main)
//   --base-exe <path>     compare: a base sandbox binary instead of --base
//   --scenes <a,b>        the scenes (default every perf_ scene)
//   --runs <n>            launches per scene and build (default RUNS; 20 for noise)
//   --warmup <seconds>    before the capture (default 3)
//   --window <seconds>    the capture (default 5)
//   --port <port>         the dev server's port (default 8094; never 8090, which other sessions use)
//   --threshold <pct>     compare: one threshold for every gate in place of GATES'; required unpinned
//   --allow-counts        compare: report count increases instead of failing on them
//   --out <path>          where the JSON goes (default under perf-results/local/)
//   --record              run: write perf-results/<machine>/<tag>.json, the file a tag's result is committed
//                         as; refused unless HEAD is exactly a tag with nothing uncommitted, or with --exe
//   --wait-busy <seconds> how long to wait before a run for another build or sandbox to finish and the CPUs it
//                         runs on to fall below BUSY_LOAD_PCT busy (default 900)
//   --affinity <mask>     the logical CPUs the sandbox may run on, a hex mask (default: defaultAffinity());
//                         none to leave it to the scheduler
//
// Exit codes: 0 within the gates, 1 a regression, 2 invalid data or a run that couldn't be made (a bad
// option, a missing scene, a port in use, a run with vsync on or too few frames), 3 inconclusive (a side
// spread wider than its gate, a run that went ahead on a busy machine, scene content that differs).
//
// The sandbox is built Release with SALAL_DEV_SERVER_IN_RELEASE in build-perf/, beside build/, so a perf
// run measures what ships and leaves the developer's build alone. Each run launches the sandbox with
// --scene, --http-port and --no-vsync, waits for /api/health, warms up, captures a fixed window through
// /api/control?action=capture, reads /api/metrics, and exits through /api/control?action=exit.

import { execFileSync, spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SCENES = ['perf_quads', 'perf_text', 'perf_world'];
const FORBIDDEN_PORT = 8090;
const EXIT = { pass: 0, regressed: 1, invalid: 2, inconclusive: 3 };

// The gate: a build's figure for a statistic is the fastest of its RUNS runs, which interference only
// slows, and a head figure more than the scene's threshold (percent) above the base's fails compare. A
// side whose two fastest runs differ by more than the threshold hasn't reproduced its figure, and the
// compare is inconclusive. Set from measured noise; docs/testing/README.md has the error rates.
// perf_world's GPU time isn't gated: launch to launch it runs at one of two levels a third apart.
const RUNS = 5;
const STATISTIC = 'min';
const GATES = {
	perf_quads: { 'cpuMs.p50': 3, 'renderMs.p50': 3, 'gpuMs.p50': 3 },
	perf_text: { 'cpuMs.p50': 3, 'renderMs.p50': 3, 'gpuMs.p50': 3 },
	perf_world: { 'cpuMs.p50': 3, 'renderMs.p50': 3 },
};
// Deterministic for fixed content: a head that draws more, uploads more or makes more draw calls fails,
// unless --allow-counts.
const COUNTS = ['drawCalls.p50', 'uploadBytes.p50', 'instances.p50', 'vertices.p50'];
// Reported beside them, not gated.
const REPORTED = [
	'cpuMs.p99',
	'renderMs.p99',
	'gpuMs.p50',
	'gpuMs.p99',
	'frameMs.p50',
	'frameMs.p99',
	'presentMs.p50',
	'presentMs.mean',
	'flushMs.p50',
	'triangles.p50',
];
const MIN_FRAMES = 200;
const MIN_GPU_COVERAGE = 0.9; // GPU results per frame, for a gated GPU statistic
const BUSY_LOAD_PCT = 25;
const BUSY = ['cl.exe', 'link.exe', 'ninja.exe', 'sandbox.exe', 'cl', 'ninja', 'cc1plus', 'clang', 'clang++', 'ld', 'sandbox'];
const FLAGS = ['record', 'allow-counts'];

class Invalid extends Error {}

const gatedKeys = (scene) => Object.keys(GATES[scene]);
const allKeys = (scene) => [...new Set([...gatedKeys(scene), ...COUNTS, ...REPORTED])];

function fail(message, code = EXIT.invalid) {
	console.error(`perf: ${message}`);
	process.exit(code);
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function run(command, args, cwd) {
	return execFileSync(command, args, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
}

// --- Options ---

function parseArgs(argv) {
	const [mode = 'run', ...rest] = argv;
	const options = {};
	for (let i = 0; i < rest.length; ++i) {
		const arg = rest[i];
		if (!arg.startsWith('--')) {
			throw new Invalid(`unexpected argument ${arg}`);
		}
		const equals = arg.indexOf('=');
		const key = equals < 0 ? arg.slice(2) : arg.slice(2, equals);
		if (FLAGS.includes(key)) {
			options[key] = true;
		} else if (equals >= 0) {
			options[key] = arg.slice(equals + 1);
		} else if (i + 1 < rest.length) {
			options[key] = rest[++i];
		} else {
			throw new Invalid(`--${key} needs a value`);
		}
	}
	return { mode, options };
}

function number(options, key, fallback, { integer = false, min = 0, exclusive = true } = {}) {
	if (options[key] === undefined) {
		return fallback;
	}
	const value = Number(options[key]);
	const low = exclusive ? value <= min : value < min;
	if (!Number.isFinite(value) || low || (integer && !Number.isInteger(value))) {
		throw new Invalid(`--${key} ${options[key]} isn't a ${integer ? 'whole number' : 'number'} ${exclusive ? 'above' : 'from'} ${min}`);
	}
	return value;
}

// Pinned to a fixed set of cores, a launch can't land on a different core or CCD from the last, which
// on a 5950X moved a run's CPU time by up to a third. Logical CPUs 4 to 11 where there are 16 or more,
// clear of CPU 0's interrupts and inside the first CCD; none on a smaller machine or on macOS, which
// has no affinity, and the runner says so.
function defaultAffinity() {
	return os.cpus().length >= 16 && process.platform !== 'darwin' ? '0x0FF0' : null;
}

function settingsFrom(mode, options) {
	const port = number(options, 'port', 8094, { integer: true });
	if (port === FORBIDDEN_PORT || port > 65535) {
		throw new Invalid(`port ${port}: 8090 is the sandbox default other sessions use, and a port is at most 65535`);
	}
	const scenes = options.scenes ? options.scenes.split(',') : SCENES;
	const unknown = scenes.filter((scene) => !SCENES.includes(scene));
	if (unknown.length > 0 || scenes.length === 0) {
		throw new Invalid(`--scenes takes ${SCENES.join(', ')}; not ${unknown.join(', ') || 'none'}`);
	}
	const affinity = options.affinity === 'none' ? null : (options.affinity ?? defaultAffinity());
	if (affinity !== null && !/^0x[0-9a-fA-F]+$/.test(affinity)) {
		throw new Invalid(`--affinity ${affinity} isn't a hex mask such as 0x0550, or none`);
	}
	if (affinity !== null && process.platform === 'darwin') {
		throw new Invalid('macOS has no CPU affinity; pass --affinity none');
	}
	const threshold = options.threshold === undefined ? null : number(options, 'threshold', null);
	if (affinity === null) {
		console.warn('perf: unpinned; launch-to-launch noise is far larger (docs/testing/README.md)');
		if (mode === 'compare' && threshold === null) {
			throw new Invalid('an unpinned compare needs its own --threshold, measured with noise --affinity none');
		}
	}
	const runs = number(options, 'runs', mode === 'noise' ? 20 : RUNS, { integer: true, min: mode === 'run' ? 1 : 3, exclusive: false });
	return {
		port,
		scenes,
		runs,
		affinity,
		threshold,
		warmup: number(options, 'warmup', 3),
		window: number(options, 'window', 5),
		waitBusy: number(options, 'wait-busy', 900, { exclusive: false }),
		allowCounts: options['allow-counts'] === true,
	};
}

// --- Builds ---

function exeIn(buildDir) {
	return process.platform === 'win32' ? path.join(buildDir, 'apps/sandbox/Release/sandbox.exe') : path.join(buildDir, 'apps/sandbox/sandbox');
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

// The commit to compare against: --base as given, warned about when it isn't an ancestor of HEAD, or
// by default where HEAD left main, so commits landed on main since don't count against the change.
function baseCommit(options) {
	const head = run('git', ['rev-parse', 'HEAD'], ROOT);
	if (options.base === undefined) {
		return { ref: 'merge-base of HEAD and main', commit: run('git', ['merge-base', 'HEAD', 'main'], ROOT), head };
	}
	const commit = run('git', ['rev-parse', '--verify', `${options.base}^{commit}`], ROOT);
	try {
		run('git', ['merge-base', '--is-ancestor', commit, head], ROOT);
	} catch {
		console.warn(`perf: ${options.base} isn't an ancestor of HEAD; the difference includes whatever it has that HEAD doesn't`);
	}
	return { ref: options.base, commit, head };
}

// A scratch worktree of `commit` outside the repo, one per checkout and ref, kept so its next build is
// incremental. A worktree whose folder was cleaned away is pruned first.
function baseCheckout(ref, commit) {
	const repo = createHash('sha1').update(ROOT.toLowerCase()).digest('hex').slice(0, 8);
	const dir = path.join(os.tmpdir(), 'salal-perf', repo, ref.replace(/[^A-Za-z0-9._-]/g, '_'));
	run('git', ['worktree', 'prune'], ROOT);
	if (fs.existsSync(path.join(dir, '.git'))) {
		run('git', ['checkout', '--detach', '--force', commit], dir);
	} else {
		fs.rmSync(dir, { recursive: true, force: true });
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
	let tag = null;
	try {
		tag = run('git', ['describe', '--tags', '--exact-match'], dir);
	} catch {}
	const dirty = run('git', ['status', '--porcelain', '--untracked-files=no'], dir) !== '';
	return { commit, describe: dirty ? `${name}-dirty` : name, tag: dirty ? null : tag };
}

// --- The machine ---

function gpuName() {
	try {
		if (process.platform === 'win32') {
			return run('powershell', ['-NoProfile', '-Command', '(Get-CimInstance Win32_VideoController).Name -join "; "']);
		}
		if (process.platform === 'darwin') {
			return run('system_profiler', ['SPDisplaysDataType']).match(/Chipset Model: (.*)/)?.[1] ?? null;
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

// The logical CPUs in a hex affinity mask; every one without a mask.
function cpusIn(affinity) {
	const all = os.cpus().map((_, i) => i);
	if (affinity === null) {
		return all;
	}
	const mask = BigInt(affinity);
	return all.filter((i) => ((mask >> BigInt(i)) & 1n) === 1n);
}

const cpuTimes = () => os.cpus().map((cpu) => cpu.times);

// How busy `cpus` were between two cpuTimes(), as a percentage of their time.
function busyPct(before, after, cpus) {
	const spent = (t) => t.user + t.nice + t.sys + t.irq;
	let busy = 0;
	let total = 0;
	for (const i of cpus) {
		busy += spent(after[i]) - spent(before[i]);
		total += spent(after[i]) + after[i].idle - spent(before[i]) - before[i].idle;
	}
	return total > 0 ? Math.round((1000 * busy) / total) / 10 : 0;
}

async function load(cpus) {
	const before = cpuTimes();
	await sleep(1000);
	return busyPct(before, cpuTimes(), cpus);
}

// Another session's build, tests or sandbox steal the cores a run measures: wait for them, and note a
// wait that ran out.
async function waitForQuiet(settings) {
	const deadline = Date.now() + settings.waitBusy * 1000;
	const cpus = cpusIn(settings.affinity);
	let busy = busyProcesses();
	let loadPct = await load(cpus);
	const noisy = () => busy.length > 0 || loadPct > BUSY_LOAD_PCT;
	if (noisy()) {
		console.log(`perf: waiting for ${[...busy, `${loadPct.toFixed(0)}% load`].join(', ')}`);
	}
	while (noisy() && Date.now() < deadline) {
		await sleep(4000);
		busy = busyProcesses();
		loadPct = await load(cpus);
	}
	if (noisy()) {
		console.warn(`perf: going ahead with ${[...busy, `${loadPct.toFixed(0)}% load`].join(', ')}; the run is flagged`);
	}
	return { busy, loadPct, timedOut: noisy() };
}

// --- One launch ---

let liveChild = null;
const killLiveChild = () => {
	if (liveChild !== null && liveChild.exitCode === null) {
		liveChild.kill();
	}
};
process.on('exit', killLiveChild);
for (const signal of ['SIGINT', 'SIGTERM']) {
	process.on(signal, () => {
		killLiveChild();
		process.exit(EXIT.invalid);
	});
}

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
	const quiet = await waitForQuiet(settings);
	if (!(await portFree(port))) {
		throw new Invalid(`port ${port} is in use; pass --port`);
	}
	const args = [`--scene=${scene}`, '--http-port', String(port), '--no-vsync'];
	const taskset = settings.affinity !== null && process.platform === 'linux';
	const child = spawn(taskset ? 'taskset' : exe, taskset ? [settings.affinity, exe, ...args] : args, {
		cwd: path.dirname(exe),
		stdio: ['ignore', 'ignore', 'pipe'],
	});
	liveChild = child;
	let stderr = '';
	child.stderr.on('data', (chunk) => (stderr = (stderr + chunk).slice(-4000)));
	let failed = null;
	child.once('error', (error) => (failed = error));
	const exited = new Promise((resolve) => child.once('exit', resolve));
	const gone = () => child.exitCode !== null || child.signalCode !== null || failed !== null;

	try {
		if (settings.affinity !== null && process.platform === 'win32') {
			try {
				run('powershell', ['-NoProfile', '-Command', `(Get-Process -Id ${child.pid}).ProcessorAffinity = ${settings.affinity}`]);
			} catch (error) {
				throw new Invalid(gone() ? `the sandbox exited at launch\n${stderr}` : `couldn't pin the sandbox: ${error.message}`);
			}
		}
		const deadline = Date.now() + 30000;
		for (;;) {
			if (failed !== null) {
				throw new Invalid(`couldn't start ${exe}: ${failed.message}`);
			}
			if (gone()) {
				throw new Invalid(`the sandbox exited before serving; does this build have ${scene}?\n${stderr}`);
			}
			try {
				await get(port, '/api/health', 1000);
				break;
			} catch {
				if (Date.now() > deadline) {
					throw new Invalid('the sandbox never answered /api/health');
				}
				await sleep(100);
			}
		}
		const top = (await get(port, '/api/state?what=scene')).scenes?.[0];
		if (top?.name !== scene) {
			throw new Invalid(`asked for ${scene}, the sandbox runs ${top?.name}`);
		}
		await sleep(settings.warmup * 1000);
		await get(port, '/api/control?action=capture&op=start');
		const before = cpuTimes();
		await sleep(settings.window * 1000);
		const after = cpuTimes();
		const { capture } = await get(port, '/api/control?action=capture&op=stop');
		const live = await get(port, '/api/metrics');
		capture.live = { drawCalls: live.drawCalls, vertexCount: live.vertexCount, uploadBytes: live.uploadBytes, flushCount: live.flushCount };
		capture.content = top.state;
		capture.busy = quiet.busy;
		capture.loadBeforePct = quiet.loadPct;
		capture.quietTimedOut = quiet.timedOut;
		// The rest of the machine meanwhile: another session's work there shares caches, memory and power.
		const pinned = new Set(cpusIn(settings.affinity));
		const others = os.cpus().map((_, i) => i).filter((i) => settings.affinity === null || !pinned.has(i));
		capture.otherCpusLoadPct = busyPct(before, after, others);
		// A socket closed before the reply loses nothing: the run is in hand, and the wait below ends it.
		await get(port, '/api/control?action=exit').catch(() => {});
		return capture;
	} finally {
		if (!gone()) {
			await Promise.race([exited, sleep(15000)]);
		}
		killLiveChild();
		liveChild = null;
	}
}

// What makes a run unusable: too few frames, frames it didn't draw, vsync on, a resize.
function problems(capture) {
	const found = [];
	if (!(capture.frames >= MIN_FRAMES)) {
		found.push(`${capture.frames} frames, under ${MIN_FRAMES}`);
	}
	if (capture.minimizedFrames > 0) {
		found.push(`${capture.minimizedFrames} minimized frames`);
	}
	if (capture.vsync !== false) {
		found.push('vsync on (a settings file can override --no-vsync)');
	}
	if (capture.resized) {
		found.push('the window resized during the capture');
	}
	return found;
}

async function runs(exe, scene, settings, count, label) {
	const out = [];
	for (let r = 0; r < count; ++r) {
		console.log(`perf: ${scene}${label} run ${r + 1}/${count}`);
		const capture = await launch(exe, scene, settings);
		const found = problems(capture);
		if (found.length > 0) {
			throw new Invalid(`${scene} run ${r + 1}: ${found.join('; ')}`);
		}
		out.push(capture);
	}
	return out;
}

// --- Statistics ---

const pick = (summary, key) => key.split('.').reduce((value, part) => value?.[part], summary);

function median(values) {
	const sorted = [...values].sort((a, b) => a - b);
	const mid = Math.floor(sorted.length / 2);
	return sorted.length % 2 === 1 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
}

const statistics = { min: (values) => Math.min(...values), median };

// Every run's value of `key`, or the reason they can't be used: each must be a finite positive number,
// and a GPU figure needs its runs' GPU results to cover most of their frames.
function valuesOf(captures, key) {
	const values = captures.map((capture) => pick(capture, key));
	if (values.some((value) => typeof value !== 'number' || !Number.isFinite(value) || value <= 0)) {
		return { error: `${key}: ${values.map(String).join(', ')}, not all finite and positive` };
	}
	if (key.startsWith('gpuMs.')) {
		const low = captures.find((capture) => capture.gpuFrames / capture.frames < MIN_GPU_COVERAGE);
		if (low !== undefined) {
			return { error: `${key}: GPU results for ${low.gpuFrames} of ${low.frames} frames, under ${MIN_GPU_COVERAGE * 100}%` };
		}
	}
	return { values };
}

const spreadPct = (values) => (100 * (Math.max(...values) - Math.min(...values))) / median(values);

// How far the second-fastest run is above the fastest: under the gate, the fastest is reproduced.
function reproducedPct(values) {
	const [fastest, next] = [...values].sort((a, b) => a - b);
	return (100 * (next - fastest)) / fastest;
}

function summarise(captures, keys) {
	const out = {};
	for (const key of keys) {
		const values = captures.map((capture) => pick(capture, key)).filter((v) => typeof v === 'number' && Number.isFinite(v));
		out[key] = values.length === captures.length ? { [STATISTIC]: statistics[STATISTIC](values), median: median(values), spreadPct: spreadPct(values) } : null;
	}
	return out;
}

// How often the gate errs, by resampling these runs with replacement: a compare of the build against
// itself failing (no-op), and a head slower by `pct` passing (missed).
function errorRates(values, statistic, thresholdPct, trials = 100000) {
	const draw = () => statistics[statistic](Array.from({ length: RUNS }, () => values[Math.floor(Math.random() * values.length)]));
	let noOp = 0;
	const missed = { 5: 0, 10: 0 };
	for (let i = 0; i < trials; ++i) {
		const base = draw();
		if ((100 * (draw() - base)) / base > thresholdPct) {
			++noOp;
		}
		for (const pct of Object.keys(missed)) {
			if ((100 * (draw() * (1 + pct / 100) - base)) / base <= thresholdPct) {
				++missed[pct];
			}
		}
	}
	return { noOpFailPct: (100 * noOp) / trials, missed5Pct: (100 * missed[5]) / trials, missed10Pct: (100 * missed[10]) / trials };
}

// --- Modes ---

function header(settings, captures) {
	return {
		schema: 2,
		date: new Date().toISOString(),
		machine: machine(),
		settings: {
			warmupSeconds: settings.warmup,
			windowSeconds: settings.window,
			runs: settings.runs,
			statistic: STATISTIC,
			affinity: settings.affinity,
			vsync: captures.some((capture) => capture.vsync),
			framebuffer: captures[0]?.framebuffer ?? null,
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
const signed = (pct) => (pct === null ? '-' : `${pct >= 0 ? '+' : ''}${pct.toFixed(1)}%`);

async function runMode(options) {
	const settings = settingsFrom('run', options);
	const record = options.record === true;
	if (record && options.exe) {
		throw new Invalid('--record measures this checkout, so it takes no --exe');
	}
	const source = options.exe ? null : describe(ROOT);
	if (record && source.tag === null) {
		throw new Invalid(`--record measures a tag: HEAD (${source.describe}) is no tag, or has uncommitted changes`);
	}
	const exe = options.exe ? path.resolve(options.exe) : build(ROOT);
	const scenes = {};
	for (const scene of settings.scenes) {
		const captures = await runs(exe, scene, settings, settings.runs, '');
		scenes[scene] = { summary: summarise(captures, allKeys(scene)), runs: captures };
		console.log(`  ${gatedKeys(scene).map((k) => `${k} ${fmt(scenes[scene].summary[k]?.[STATISTIC])}`).join('  ')}`);
	}
	const result = { ...header(settings, Object.values(scenes).flatMap((s) => s.runs)), source, scenes };
	const file = record ? path.join(ROOT, 'perf-results', machine().name, `${source.tag}.json`) : path.join(ROOT, 'perf-results/local', `${source?.describe ?? 'exe'}.json`);
	write(options.out ?? file, result);
}

async function noiseMode(options) {
	const settings = settingsFrom('noise', options);
	const exe = options.exe ? path.resolve(options.exe) : build(ROOT);
	const scenes = {};
	for (const scene of settings.scenes) {
		const captures = await runs(exe, scene, settings, settings.runs, '');
		const stats = {};
		console.log(`  ${scene}`);
		for (const key of [...new Set([...gatedKeys(scene), 'gpuMs.p50'])]) {
			const { values, error } = valuesOf(captures, key);
			if (error) {
				console.log(`    ${key.padEnd(14)} ${error}`);
				continue;
			}
			const mean = values.reduce((a, b) => a + b, 0) / values.length;
			const sd = Math.sqrt(values.reduce((a, b) => a + (b - mean) ** 2, 0) / (values.length - 1));
			const gates = {};
			for (const statistic of Object.keys(statistics)) {
				for (const threshold of [2, 3, 4, 5]) {
					gates[`${statistic}@${threshold}`] = errorRates(values, statistic, threshold);
				}
			}
			stats[key] = { median: median(values), min: Math.min(...values), rangePct: spreadPct(values), cvPct: (100 * sd) / mean, gates, values };
			const at = (g) => `${g.noOpFailPct.toFixed(1)}/${g.missed5Pct.toFixed(1)}`;
			console.log(
				`    ${key.padEnd(14)} median ${fmt(median(values))} min ${fmt(Math.min(...values))} range ${spreadPct(values).toFixed(1)}% cv ${stats[key].cvPct.toFixed(1)}%` +
					`  no-op fail / +5% missed: min@3 ${at(gates['min@3'])}  min@4 ${at(gates['min@4'])}  median@3 ${at(gates['median@3'])}  median@4 ${at(gates['median@4'])}`
			);
		}
		scenes[scene] = { stats, runs: captures };
	}
	const result = { ...header(settings, Object.values(scenes).flatMap((s) => s.runs)), source: options.exe ? null : describe(ROOT), scenes };
	write(options.out ?? path.join(ROOT, 'perf-results/local', `noise-${Date.now()}.json`), result);
}

async function compareMode(options) {
	const settings = settingsFrom('compare', options);
	const headExe = options.exe ? path.resolve(options.exe) : build(ROOT);
	let baseExe = options['base-exe'] ? path.resolve(options['base-exe']) : null;
	let base = { exe: baseExe };
	if (!baseExe) {
		const resolved = baseCommit(options);
		baseExe = build(baseCheckout(options.base ?? 'merge-base', resolved.commit));
		base = { exe: baseExe, ref: resolved.ref, commit: resolved.commit };
		console.log(`perf: base ${resolved.commit.slice(0, 12)} (${resolved.ref}), head ${resolved.head.slice(0, 12)}`);
	}

	const verdicts = [];
	const scenes = {};
	for (const scene of settings.scenes) {
		const baseRuns = [];
		const headRuns = [];
		// Interleaved, alternating which goes first, so drift over the session lands on both.
		for (let r = 0; r < settings.runs; ++r) {
			const order = r % 2 === 0 ? ['base', 'head'] : ['head', 'base'];
			for (const side of order) {
				const [capture] = await runs(side === 'base' ? baseExe : headExe, scene, settings, 1, ` ${side} ${r + 1}/${settings.runs},`);
				(side === 'base' ? baseRuns : headRuns).push(capture);
			}
		}

		const rows = {};
		const notes = [];
		let verdict = 'pass';
		const worsen = (to) => {
			const rank = { pass: 0, inconclusive: 1, regressed: 2, invalid: 3 };
			if (rank[to] > rank[verdict]) {
				verdict = to;
			}
		};
		const gpuSupported = [...baseRuns, ...headRuns].every((capture) => capture.gpuSupported !== false);
		for (const [key, gate] of Object.entries(GATES[scene])) {
			const threshold = settings.threshold ?? gate;
			if (key.startsWith('gpuMs.') && !gpuSupported) {
				notes.push(`${key}: no GPU timestamps on this machine, not gated`);
				continue;
			}
			const b = valuesOf(baseRuns, key);
			const h = valuesOf(headRuns, key);
			if (b.error || h.error) {
				notes.push(`invalid: ${b.error ?? h.error}`);
				worsen('invalid');
				continue;
			}
			const baseFigure = statistics[STATISTIC](b.values);
			const headFigure = statistics[STATISTIC](h.values);
			const deltaPct = (100 * (headFigure - baseFigure)) / baseFigure;
			const spreads = { base: spreadPct(b.values), head: spreadPct(h.values) };
			const gaps = { base: reproducedPct(b.values), head: reproducedPct(h.values) };
			let state = deltaPct > threshold ? 'regressed' : 'pass';
			if (Math.max(gaps.base, gaps.head) > threshold) {
				state = 'inconclusive';
				notes.push(`${key}: a side's two fastest runs differ by ${Math.max(gaps.base, gaps.head).toFixed(1)}%, past its ${threshold}% gate`);
			}
			worsen(state);
			rows[key] = { base: baseFigure, head: headFigure, deltaPct, threshold, spreads, gaps, state, baseValues: b.values, headValues: h.values };
		}
		for (const key of COUNTS) {
			const b = median(baseRuns.map((capture) => pick(capture, key) ?? 0));
			const h = median(headRuns.map((capture) => pick(capture, key) ?? 0));
			const state = h > b && !settings.allowCounts ? 'regressed' : 'pass';
			worsen(state);
			rows[key] = { base: b, head: h, deltaPct: b > 0 ? (100 * (h - b)) / b : null, state };
		}
		for (const key of REPORTED.filter((k) => rows[k] === undefined)) {
			const b = baseRuns.map((capture) => pick(capture, key)).filter(Number.isFinite);
			const h = headRuns.map((capture) => pick(capture, key)).filter(Number.isFinite);
			const bm = b.length > 0 ? median(b) : null;
			const hm = h.length > 0 ? median(h) : null;
			rows[key] = { base: bm, head: hm, deltaPct: bm && hm !== null ? (100 * (hm - bm)) / bm : null, state: 'reported' };
		}
		const content = (runsOf) => JSON.stringify(runsOf[0]?.content ?? null);
		if (content(baseRuns) !== content(headRuns)) {
			notes.push(`the scene's content differs: base ${content(baseRuns)}, head ${content(headRuns)}`);
			worsen('inconclusive');
		}
		const flagged = [...baseRuns, ...headRuns].filter((capture) => capture.quietTimedOut).length;
		if (flagged > 0) {
			notes.push(`${flagged} runs went ahead on a busy machine`);
			worsen('inconclusive');
		}

		console.log(`  ${scene}: ${verdict}`);
		for (const [key, row] of Object.entries(rows)) {
			const spread = row.spreads
				? `  spread ${row.spreads.base.toFixed(1)}% / ${row.spreads.head.toFixed(1)}%, two fastest ${row.gaps.base.toFixed(1)}% / ${row.gaps.head.toFixed(1)}%`
				: '';
			const gate = row.threshold ? `  gate ${row.threshold}%` : '';
			const flag = ['regressed', 'inconclusive'].includes(row.state) ? `  ${row.state.toUpperCase()}` : '';
			console.log(`    ${key.padEnd(16)} ${fmt(row.base).padStart(12)} ${fmt(row.head).padStart(12)} ${signed(row.deltaPct).padStart(8)}${gate}${spread}${flag}`);
		}
		for (const note of notes) {
			console.log(`    ${note}`);
		}
		verdicts.push(verdict);
		scenes[scene] = { verdict, metrics: rows, notes, base: baseRuns, head: headRuns };
	}

	const overall = ['invalid', 'regressed', 'inconclusive'].find((v) => verdicts.includes(v)) ?? 'pass';
	const result = {
		...header(settings, Object.values(scenes).flatMap((s) => [...s.base, ...s.head])),
		statistic: STATISTIC,
		gates: settings.threshold === null ? GATES : `${settings.threshold}% everywhere`,
		base,
		head: { exe: headExe, source: options.exe ? null : describe(ROOT) },
		verdict: overall,
		scenes,
	};
	write(options.out ?? path.join(ROOT, 'perf-results/local', `compare-${Date.now()}.json`), result);
	console.log(`perf: ${overall}`);
	process.exit(EXIT[overall === 'pass' ? 'pass' : overall]);
}

const modes = { run: runMode, compare: compareMode, noise: noiseMode };
try {
	const { mode, options } = parseArgs(process.argv.slice(2));
	if (!modes[mode]) {
		throw new Invalid(`unknown mode ${mode}; run, compare or noise`);
	}
	await modes[mode](options);
} catch (error) {
	fail(error instanceof Invalid ? error.message : (error.stack ?? String(error)));
}
