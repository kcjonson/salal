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
//                         merge-base of HEAD and origin/main, fetched first, or of HEAD and main without one)
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
//   --wait-busy <seconds> how long to wait before a run for builds, tests and other apps to finish and the
//                         CPUs it runs on to fall below BUSY_LOAD_PCT busy (default 900)
//   --affinity <mask>     the logical CPUs the sandbox may run on, a hex mask (default: defaultAffinity());
//                         none to leave it to the scheduler
//   --note <text>         written into the result, for what the runner can't see (another machine's load)
//   --from <file>         noise: calibrate the runs of an earlier noise result instead of making new ones
//
// Exit codes: 0 within the gates, 1 a regression, 2 invalid data or a run that couldn't be made (a bad
// option, a missing scene, a port in use, a sandbox that didn't start, a run with vsync on or too few
// frames), 3 inconclusive (a side's spread wider than its gate, a run that went ahead on a busy machine
// or was still disturbed after its retries, scene content or window size that differs).
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
const SCENES = ['perf_quads', 'perf_text', 'perf_world', 'perf_ui'];
const FORBIDDEN_PORT = 8090;
const EXIT = { pass: 0, regressed: 1, invalid: 2, inconclusive: 3 };

// The gate: a build's figure for a timing is the STATISTIC of its RUNS runs' values, and a head figure
// more than the scene's threshold (percent) above the base's fails compare. A side whose spread (below)
// is wider than the threshold hasn't reproduced its figure, and the compare is inconclusive. Set from
// 20 runs a scene on an idle machine, every split of them judged; docs/testing/README.md has the error
// rates behind each number. The fastest of five: interference only slows a run, and the median left
// half of perf_quads' no-op compares inconclusive. perf_ui's walk varies more launch to launch.
const RUNS = 5;
const STATISTIC = 'min';
const GATES = {
	perf_quads: { 'cpuMs.p50': 3, 'renderMs.p50': 3, 'gpuMs.p50': 3 },
	perf_text: { 'cpuMs.p50': 3, 'renderMs.p50': 3, 'gpuMs.p50': 3 },
	perf_world: { 'cpuMs.p50': 3, 'renderMs.p50': 3, 'gpuMs.p50': 3 },
	perf_ui: { 'cpuMs.p50': 4, 'renderMs.p50': 4, 'gpuMs.p50': 4 },
};
// Fixed for fixed content: a head that draws more, uploads more or makes more draw calls fails, unless
// --allow-counts. A moving camera culls a little differently in each capture window, so a count fails
// only when every head run is above every base run.
const COUNTS = ['drawCalls.p50', 'uploadBytes.p50', 'instances.p50', 'vertices.p50'];
// Reported beside them, not gated.
const REPORTED = [
	'cpuMs.p99',
	'renderMs.p99',
	'gpuMs.p50',
	'gpuMs.p99',
	'layoutMs.mean',
	'flushMs.p50',
	'frameMs.p50',
	'frameMs.p99',
	'presentMs.p50',
	'presentMs.mean',
	'triangles.p50',
	'gpuDropped',
];
const MIN_FRAMES = 200;
const MIN_GPU_COVERAGE = 0.9; // GPU results per frame, for a gated GPU statistic
const BUSY_LOAD_PCT = 25;
// Past this on the CPUs a run isn't pinned to, during its capture, something else ran meanwhile (a build
// starting): the run is made again, up to ATTEMPTS times, then kept and flagged.
const OTHER_LOAD_PCT = 20;
const ATTEMPTS = 3;
// Builds, tests and apps that would share the machine with a run.
const BUSY =
	/^(cl|link|lld-link|clang|clang\+\+|clang-cl|cc1plus|ld|ninja|cmake|ctest|msbuild|sandbox|world-sim|ui-sandbox|asset-manager|.*_tests)(\.exe)?$/;
const FLAGS = ['record', 'allow-counts'];
const THRESHOLDS = [1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10];

class Invalid extends Error {}

const gatedKeys = (scene) => Object.keys(GATES[scene]);
const allKeys = (scene) => [...new Set([...gatedKeys(scene), ...COUNTS, ...REPORTED])];

function fail(message, code = EXIT.invalid) {
	console.error(`perf: ${message}`);
	process.exit(code);
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function run(command, args, cwd, timeout = 0) {
	return execFileSync(command, args, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], timeout }).trim();
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
	if (options[key] === '' || !Number.isFinite(value) || low || (integer && !Number.isInteger(value))) {
		throw new Invalid(`--${key} ${options[key]} isn't a ${integer ? 'whole number' : 'number'} ${exclusive ? 'above' : 'from'} ${min}`);
	}
	return value;
}

// Pinned to a fixed set of cores, a launch can't land on a different core or CCD from the last. One
// logical CPU on each of four cores, 4, 6, 8 and 10, where there are 16 or more: clear of CPU 0's
// interrupts, inside a 5950X's first CCD, and no two of the sandbox's threads sharing a core; the
// thresholds were set on it, and 0x0FF0, both threads of the same cores, is still to be measured against
// it (README, Performance). None on a smaller machine or on macOS, which has no affinity, and the runner
// says so.
function defaultAffinity() {
	return os.cpus().length >= 16 && process.platform !== 'darwin' ? '0x0550' : null;
}

function settingsFrom(mode, options) {
	const known = [
		'exe',
		'base',
		'base-exe',
		'scenes',
		'runs',
		'warmup',
		'window',
		'port',
		'threshold',
		'allow-counts',
		'out',
		'record',
		'wait-busy',
		'affinity',
		'note',
		'from',
	];
	const unknownOption = Object.keys(options).find((key) => !known.includes(key));
	if (unknownOption !== undefined) {
		throw new Invalid(`unknown option --${unknownOption}`);
	}
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
	if (affinity !== null && cpusIn(affinity).length === 0) {
		throw new Invalid(`--affinity ${affinity} names none of this machine's ${os.cpus().length} logical CPUs`);
	}
	const threshold = options.threshold === undefined ? null : number(options, 'threshold', null);
	if (affinity === null) {
		console.warn('perf: unpinned; launch-to-launch noise is far larger (docs/testing/README.md)');
		if (mode === 'compare' && threshold === null) {
			throw new Invalid('an unpinned compare needs its own --threshold, measured with noise --affinity none');
		}
	}
	const runs = number(options, 'runs', mode === 'noise' ? 20 : RUNS, {
		integer: true,
		min: mode === 'run' ? 1 : mode === 'noise' ? 2 * RUNS : 3,
		exclusive: false,
	});
	if (options.note !== undefined && options.note.trim() === '') {
		throw new Invalid('--note needs some text');
	}
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
		note: options.note ?? null,
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

// A sandbox binary as given, which must exist.
function exeOption(value, option) {
	const exe = path.resolve(value);
	if (!fs.existsSync(exe)) {
		throw new Invalid(`--${option} ${value}: no such file`);
	}
	return exe;
}

// The scenes a sandbox binary has, from its --help.
function scenesOf(exe) {
	let usage = '';
	try {
		usage = run(exe, ['--help'], path.dirname(exe), 30000);
	} catch (error) {
		throw new Invalid(`couldn't run ${exe} --help: ${error.message}`);
	}
	const line = usage.split(/\r?\n/).find((text) => text.startsWith('Scenes:'));
	if (line === undefined) {
		throw new Invalid(`${exe} --help lists no scenes`);
	}
	return line.slice('Scenes:'.length).trim().split(/\s+/);
}

// main as the remote has it, fetched first so a stale local main doesn't make the base older than where
// HEAD left it; local main when there is no remote one.
function upstreamMain() {
	try {
		run('git', ['fetch', '--quiet', 'origin', 'main'], ROOT, 60000);
	} catch {
		console.warn("perf: couldn't fetch origin main; using the origin/main this checkout has");
	}
	for (const ref of ['origin/main', 'main']) {
		try {
			run('git', ['rev-parse', '--verify', '--quiet', `${ref}^{commit}`], ROOT);
			return ref;
		} catch {}
	}
	throw new Invalid('there is no origin/main or main to take the merge-base with; pass --base');
}

// The commit to compare against: --base as given, warned about when it isn't an ancestor of HEAD, or by
// default where HEAD left main, so commits landed on main since don't count against the change.
function baseCommit(options) {
	const head = run('git', ['rev-parse', 'HEAD'], ROOT);
	if (options.base === undefined) {
		const main = upstreamMain();
		return { ref: `merge-base of HEAD and ${main}`, commit: run('git', ['merge-base', 'HEAD', main], ROOT), head };
	}
	let commit;
	try {
		commit = run('git', ['rev-parse', '--verify', `${options.base}^{commit}`], ROOT);
	} catch {
		throw new Invalid(`--base ${options.base} is no commit here`);
	}
	try {
		run('git', ['merge-base', '--is-ancestor', commit, head], ROOT);
	} catch {
		console.warn(`perf: ${options.base} isn't an ancestor of HEAD; the difference includes whatever it has that HEAD doesn't`);
	}
	return { ref: options.base, commit, head };
}

// A scratch worktree of `commit` outside the repo, one per checkout and ref, kept so its next build is
// incremental. Worktrees whose folders were cleaned away are pruned first, so one can be made again.
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

// The busiest processes over two seconds, as a percentage of the whole machine (on Windows; elsewhere ps's
// own figure), recorded once a session so a result says what else was running.
function topProcesses() {
	try {
		if (process.platform === 'win32') {
			const script =
				'$n = [Environment]::ProcessorCount; $a = @{}; Get-Process | ForEach-Object { $a[$_.Id] = $_.CPU }; Start-Sleep -Seconds 2; ' +
				'Get-Process | Where-Object { $a.ContainsKey($_.Id) -and $null -ne $_.CPU } | ' +
				'ForEach-Object { [pscustomobject]@{ name = $_.ProcessName; pct = [math]::Round(($_.CPU - $a[$_.Id]) / 2 / $n * 100, 1) } } | ' +
				'Sort-Object pct -Descending | Select-Object -First 6 | ConvertTo-Json -Compress';
			return JSON.parse(run('powershell', ['-NoProfile', '-Command', script], undefined, 30000));
		}
		return run('ps', ['-A', '-o', 'pcpu=,comm='])
			.split('\n')
			.map((line) => line.trim().match(/^([\d.]+)\s+(.*)$/))
			.filter(Boolean)
			.map(([, pct, name]) => ({ name: path.basename(name), pct: Number(pct) }))
			.sort((a, b) => b.pct - a.pct)
			.slice(0, 6);
	} catch {
		return null;
	}
}

function busyProcesses() {
	try {
		const names =
			process.platform === 'win32'
				? run('tasklist', ['/FO', 'CSV', '/NH'])
						.split(/\r?\n/)
						.map((line) => line.split('","')[0].replace(/^"/, '').toLowerCase())
				: run('ps', ['-A', '-o', 'comm=']).split('\n').map((line) => path.basename(line.trim()).toLowerCase());
		return [...new Set(names.filter((name) => BUSY.test(name)))];
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

// Another session's build, tests or app steal the cores a run measures: wait for them, and note a wait
// that ran out.
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
	child.stderr?.on('data', (chunk) => (stderr = (stderr + chunk).slice(-4000)));
	let failed = null;
	child.once('error', (error) => (failed = error));
	const exited = new Promise((resolve) => {
		child.once('exit', resolve);
		child.once('error', resolve);
	});
	const gone = () => child.exitCode !== null || child.signalCode !== null || failed !== null;

	try {
		if (child.pid === undefined) {
			await sleep(0); // the spawn's error arrives on the next tick
			throw new Invalid(`couldn't start ${taskset ? 'taskset' : exe}: ${failed?.message ?? 'no process'}`);
		}
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

// A run disturbed after it started: a build or another app still running once it's over, or the rest
// of the machine busy during the capture.
async function disturbance(capture) {
	const busy = busyProcesses();
	if (busy.length > 0 || capture.otherCpusLoadPct > OTHER_LOAD_PCT) {
		return [...busy, `${capture.otherCpusLoadPct}% load elsewhere`].join(', ');
	}
	return null;
}

async function runs(exe, scene, settings, count, label) {
	const out = [];
	for (let r = 0; r < count; ++r) {
		console.log(`perf: ${scene}${label} run ${r + 1}/${count}`);
		let capture;
		for (let attempt = 1; ; ++attempt) {
			capture = await launch(exe, scene, settings);
			const found = problems(capture);
			if (found.length > 0) {
				throw new Invalid(`${scene} run ${r + 1}: ${found.join('; ')}`);
			}
			capture.disturbed = await disturbance(capture);
			if (capture.disturbed === null || attempt === ATTEMPTS) {
				break;
			}
			console.log(`perf: disturbed (${capture.disturbed}); again`);
		}
		if (capture.disturbed !== null) {
			console.warn(`perf: still disturbed after ${ATTEMPTS} attempts (${capture.disturbed}); the run is flagged`);
		}
		out.push(capture);
	}
	return out;
}

// --- Statistics ---

const pick = (summary, key) => key.split('.').reduce((value, part) => value?.[part], summary);
const ascending = (values) => [...values].sort((a, b) => a - b);

function median(values) {
	const sorted = ascending(values);
	const mid = Math.floor(sorted.length / 2);
	return sorted.length % 2 === 1 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
}

const statistics = { min: (values) => Math.min(...values), median };

// How far a side is from reproducing its figure, in percent: for the fastest run, how far the next
// fastest is above it; for the median, the range of the middle runs (the second to the fourth of five)
// over it. Past the threshold, a compare is inconclusive.
function spreadPct(values, statistic) {
	const sorted = ascending(values);
	if (statistic === 'min') {
		return (100 * (sorted[1] - sorted[0])) / sorted[0];
	}
	const q = Math.floor(sorted.length / 4);
	return (100 * (sorted[sorted.length - 1 - q] - sorted[q])) / median(sorted);
}

const rangePct = (values) => (100 * (Math.max(...values) - Math.min(...values))) / median(values);

// Every run's value of `key`, or the reason they can't be used: each must be a finite positive number,
// and a GPU figure needs its runs' GPU results to cover most of their frames.
function valuesOf(captures, key) {
	const values = captures.map((capture) => pick(capture, key));
	if (values.some((value) => typeof value !== 'number' || !Number.isFinite(value) || value <= 0)) {
		return { error: `${key}: ${values.map(String).join(', ')}, not all finite and positive` };
	}
	if (key.startsWith('gpuMs.')) {
		const low = captures.find((capture) => !(capture.gpuFrames / capture.frames >= MIN_GPU_COVERAGE));
		if (low !== undefined) {
			return { error: `${key}: GPU results for ${low.gpuFrames} of ${low.frames} frames, under ${MIN_GPU_COVERAGE * 100}%` };
		}
	}
	return { values };
}

// A count's value in every run, each a finite number from 0, or why not.
function countsOf(captures, key) {
	const values = captures.map((capture) => pick(capture, key));
	if (values.some((value) => typeof value !== 'number' || !Number.isFinite(value) || value < 0)) {
		return { error: `${key}: ${values.map(String).join(', ')}, not all counts` };
	}
	return { values };
}

function summarise(captures, keys) {
	const out = {};
	for (const key of keys) {
		const values = captures.map((capture) => pick(capture, key)).filter((v) => typeof v === 'number' && Number.isFinite(v));
		out[key] =
			values.length === captures.length && values.length > 0
				? { [STATISTIC]: statistics[STATISTIC](values), median: median(values), min: Math.min(...values), rangePct: rangePct(values) }
				: null;
	}
	return out;
}

// Every way of picking `k` of `n` runs, as bit masks.
function subsetMasks(n, k) {
	const masks = [];
	const pickFrom = (start, left, mask) => {
		if (left === 0) {
			masks.push(mask);
			return;
		}
		for (let i = start; i <= n - left; ++i) {
			pickFrom(i + 1, left - 1, mask | (1 << i));
		}
	};
	pickFrom(0, k, 0);
	return masks;
}

// How the gate would have judged these runs of one build, over every split of them into a base and a
// head of RUNS runs each (a random two million splits when there are too many): a compare against itself
// failing (no-op fail) or coming out inconclusive, and the head made 5% or 10% slower failing
// (detected), at each threshold in THRESHOLDS.
function calibrate(values, statistic) {
	const n = values.length;
	if (n < 2 * RUNS || n > 30) {
		return null;
	}
	const masks = subsetMasks(n, RUNS);
	const figure = new Float64Array(masks.length);
	const spread = new Float64Array(masks.length);
	masks.forEach((mask, s) => {
		const chosen = values.filter((_, i) => ((mask >> i) & 1) === 1);
		figure[s] = statistics[statistic](chosen);
		spread[s] = spreadPct(chosen, statistic);
	});
	const counts = THRESHOLDS.map(() => ({ noOpFail: 0, noOpInconclusive: 0, detected5: 0, detected10: 0 }));
	let splits = 0;
	const judge = (a, b) => {
		++splits;
		const ratio = figure[b] / figure[a];
		const widest = Math.max(spread[a], spread[b]);
		for (let t = 0; t < THRESHOLDS.length; ++t) {
			const limit = 1 + THRESHOLDS[t] / 100;
			const c = counts[t];
			if (widest > THRESHOLDS[t]) {
				++c.noOpInconclusive;
				continue;
			}
			if (ratio > limit) {
				++c.noOpFail;
			}
			if (ratio * 1.05 > limit) {
				++c.detected5;
			}
			if (ratio * 1.1 > limit) {
				++c.detected10;
			}
		}
	};
	if (masks.length * masks.length <= 4e8) {
		for (let a = 0; a < masks.length; ++a) {
			for (let b = 0; b < masks.length; ++b) {
				if ((masks[a] & masks[b]) === 0) {
					judge(a, b);
				}
			}
		}
	} else {
		while (splits < 2e6) {
			const a = Math.floor(Math.random() * masks.length);
			const b = Math.floor(Math.random() * masks.length);
			if ((masks[a] & masks[b]) === 0) {
				judge(a, b);
			}
		}
	}
	const pct = (count) => Math.round((10000 * count) / splits) / 100;
	return THRESHOLDS.map((threshold, t) => ({
		threshold,
		noOpFailPct: pct(counts[t].noOpFail),
		noOpInconclusivePct: pct(counts[t].noOpInconclusive),
		detected5Pct: pct(counts[t].detected5),
		detected10Pct: pct(counts[t].detected10),
		splits,
	}));
}

// The tightest threshold whose no-op compares fail under 5% of the time and come out inconclusive under
// 10%, and which catches a 5% regression in most; null if none on the grid does.
function suggest(rows) {
	return rows?.find((row) => row.noOpFailPct < 5 && row.noOpInconclusivePct < 10 && row.detected5Pct > 50)?.threshold ?? null;
}

// --- Modes ---

// The machine and what else was busy on it, taken as a session starts.
function header(settings) {
	return {
		schema: 3,
		date: new Date().toISOString(),
		machine: machine(),
		load: topProcesses(),
		note: settings.note,
		build: 'Release, SALAL_DEV_SERVER_IN_RELEASE=ON',
	};
}

function settingsOf(settings, captures) {
	return {
		warmupSeconds: settings.warmup,
		windowSeconds: settings.window,
		runs: settings.runs,
		statistic: STATISTIC,
		affinity: settings.affinity,
		vsync: captures.some((capture) => capture.vsync),
		framebuffer: captures[0]?.framebuffer ?? null,
	};
}

function write(file, result) {
	fs.mkdirSync(path.dirname(file), { recursive: true });
	fs.writeFileSync(file, `${JSON.stringify(result, null, '\t')}\n`);
	console.log(`perf: wrote ${path.relative(process.cwd(), file)}`);
}

const fmt = (v) => (v === null || v === undefined ? '-' : Math.abs(v) >= 1000 ? v.toFixed(0) : v.toFixed(3));
const signed = (pct) => (pct === null || pct === undefined ? '-' : `${pct >= 0 ? '+' : ''}${pct.toFixed(1)}%`);

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
	const exe = options.exe ? exeOption(options.exe, 'exe') : build(ROOT);
	const missing = settings.scenes.filter((scene) => !scenesOf(exe).includes(scene));
	if (missing.length > 0) {
		throw new Invalid(`${exe} has no ${missing.join(', ')}`);
	}
	const top = header(settings);
	const scenes = {};
	for (const scene of settings.scenes) {
		const captures = await runs(exe, scene, settings, settings.runs, '');
		scenes[scene] = { summary: summarise(captures, allKeys(scene)), runs: captures };
		console.log(`  ${gatedKeys(scene).map((k) => `${k} ${fmt(scenes[scene].summary[k]?.[STATISTIC])}`).join('  ')}`);
	}
	const captures = Object.values(scenes).flatMap((s) => s.runs);
	const result = { ...top, settings: settingsOf(settings, captures), source, scenes };
	const file = record ? path.join(ROOT, 'perf-results', machine().name, `${source.tag}.json`) : path.join(ROOT, 'perf-results/local', `${source?.describe ?? 'exe'}.json`);
	write(options.out ?? file, result);
}

function noiseReport(scene, captures) {
	const stats = {};
	console.log(`  ${scene}`);
	for (const key of [...new Set([...gatedKeys(scene), 'gpuMs.p50'])]) {
		const { values, error } = valuesOf(captures, key);
		if (error) {
			console.log(`    ${key.padEnd(14)} ${error}`);
			stats[key] = { error };
			continue;
		}
		const mean = values.reduce((a, b) => a + b, 0) / values.length;
		const sd = Math.sqrt(values.reduce((a, b) => a + (b - mean) ** 2, 0) / (values.length - 1));
		const calibration = {};
		const suggested = {};
		for (const statistic of Object.keys(statistics)) {
			calibration[statistic] = calibrate(values, statistic);
			suggested[statistic] = suggest(calibration[statistic]);
		}
		stats[key] = { median: median(values), min: Math.min(...values), rangePct: rangePct(values), cvPct: (100 * sd) / mean, suggested, calibration, values };
		console.log(
			`    ${key.padEnd(14)} median ${fmt(median(values))} min ${fmt(Math.min(...values))} range ${rangePct(values).toFixed(1)}% cv ${stats[key].cvPct.toFixed(1)}%` +
				`  tightest threshold: min of ${RUNS} ${suggested.min ?? 'none'}%, median of ${RUNS} ${suggested.median ?? 'none'}%`
		);
		for (const statistic of Object.keys(statistics)) {
			const row = (t) => calibration[statistic]?.find((r) => r.threshold === t);
			const cells = [2, 3, 4, 5].map((t) => `${t}%: ${row(t)?.noOpFailPct}/${row(t)?.noOpInconclusivePct}/${row(t)?.detected5Pct}`);
			console.log(`      ${statistic.padEnd(6)} no-op fail/inconclusive/+5% caught  ${cells.join('  ')}`);
		}
	}
	return stats;
}

async function noiseMode(options) {
	const settings = settingsFrom('noise', options);
	if (options.from !== undefined) {
		let earlier;
		try {
			earlier = JSON.parse(fs.readFileSync(options.from, 'utf8'));
		} catch (error) {
			throw new Invalid(`--from ${options.from}: ${error.message}`);
		}
		const scenes = {};
		for (const [scene, data] of Object.entries(earlier.scenes ?? {})) {
			if (GATES[scene] && Array.isArray(data.runs)) {
				scenes[scene] = { stats: noiseReport(scene, data.runs), runs: data.runs };
			}
		}
		write(options.out ?? path.join(ROOT, 'perf-results/local', `noise-recalibrated-${Date.now()}.json`), { ...earlier, recalibrated: new Date().toISOString(), scenes });
		return;
	}
	const exe = options.exe ? exeOption(options.exe, 'exe') : build(ROOT);
	const top = header(settings);
	const scenes = {};
	for (const scene of settings.scenes) {
		const captures = await runs(exe, scene, settings, settings.runs, '');
		scenes[scene] = { stats: noiseReport(scene, captures), runs: captures };
	}
	const captures = Object.values(scenes).flatMap((s) => s.runs);
	const result = { ...top, settings: settingsOf(settings, captures), source: options.exe ? { exe } : describe(ROOT), scenes };
	write(options.out ?? path.join(ROOT, 'perf-results/local', `noise-${Date.now()}.json`), result);
}

// One scene's verdict and rows from its base and head runs.
function judgeScene(scene, baseRuns, headRuns, settings) {
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
		const spreads = { base: spreadPct(b.values, STATISTIC), head: spreadPct(h.values, STATISTIC) };
		let state = deltaPct > threshold ? 'regressed' : 'pass';
		if (Math.max(spreads.base, spreads.head) > threshold) {
			state = 'inconclusive';
			notes.push(`${key}: a side's spread is ${Math.max(spreads.base, spreads.head).toFixed(1)}%, past its ${threshold}% gate`);
		}
		worsen(state);
		rows[key] = { base: baseFigure, head: headFigure, deltaPct, threshold, spreads, state, baseValues: b.values, headValues: h.values };
	}
	for (const key of COUNTS) {
		const b = countsOf(baseRuns, key);
		const h = countsOf(headRuns, key);
		if (b.error || h.error) {
			notes.push(`invalid: ${b.error ?? h.error}`);
			worsen('invalid');
			continue;
		}
		const up = Math.min(...h.values) > Math.max(...b.values);
		const bm = median(b.values);
		const hm = median(h.values);
		const state = up && !settings.allowCounts ? 'regressed' : 'pass';
		if (up && settings.allowCounts) {
			notes.push(`${key} went up, allowed by --allow-counts`);
		}
		worsen(state);
		rows[key] = { base: bm, head: hm, deltaPct: bm > 0 ? (100 * (hm - bm)) / bm : null, state };
	}
	for (const key of REPORTED.filter((k) => rows[k] === undefined)) {
		const b = baseRuns.map((capture) => pick(capture, key)).filter(Number.isFinite);
		const h = headRuns.map((capture) => pick(capture, key)).filter(Number.isFinite);
		const bm = b.length === baseRuns.length ? statistics[STATISTIC](b) : null;
		const hm = h.length === headRuns.length ? statistics[STATISTIC](h) : null;
		rows[key] = { base: bm, head: hm, deltaPct: bm && hm !== null ? (100 * (hm - bm)) / bm : null, state: 'reported' };
	}
	const dropped = (side) => side.reduce((sum, capture) => sum + (capture.gpuDropped ?? 0), 0);
	if (dropped(baseRuns) + dropped(headRuns) > 0) {
		notes.push(`GPU results not back in time: base ${dropped(baseRuns)}, head ${dropped(headRuns)}, over the runs`);
	}
	const same = (field) => JSON.stringify(baseRuns[0]?.[field] ?? null) === JSON.stringify(headRuns[0]?.[field] ?? null);
	for (const field of ['content', 'framebuffer']) {
		if (!same(field)) {
			notes.push(`the ${field} differs: base ${JSON.stringify(baseRuns[0]?.[field])}, head ${JSON.stringify(headRuns[0]?.[field])}`);
			worsen('inconclusive');
		}
	}
	const flagged = [...baseRuns, ...headRuns].filter((capture) => capture.quietTimedOut || (capture.disturbed ?? null) !== null).length;
	if (flagged > 0) {
		notes.push(`${flagged} runs went ahead on a busy machine or were disturbed`);
		worsen('inconclusive');
	}
	return { verdict, rows, notes };
}

async function compareMode(options) {
	const settings = settingsFrom('compare', options);
	const headExe = options.exe ? exeOption(options.exe, 'exe') : build(ROOT);
	let base = {};
	let baseExe;
	if (options['base-exe']) {
		baseExe = exeOption(options['base-exe'], 'base-exe');
		base = { exe: baseExe };
		console.log(`perf: base ${baseExe}, head ${headExe}`);
	} else {
		const resolved = baseCommit(options);
		baseExe = build(baseCheckout(options.base ?? 'merge-base', resolved.commit));
		base = { exe: baseExe, ref: resolved.ref, commit: resolved.commit };
		console.log(`perf: base ${resolved.commit} (${resolved.ref})`);
		console.log(`perf: head ${resolved.head}${options.exe ? ` (--exe ${headExe})` : ''}`);
	}
	const top = header(settings);
	const baseScenes = scenesOf(baseExe);
	const headScenes = scenesOf(headExe);
	const missingHead = settings.scenes.filter((scene) => !headScenes.includes(scene));
	if (missingHead.length > 0) {
		throw new Invalid(`the head build has no ${missingHead.join(', ')}`);
	}
	if (!settings.scenes.some((scene) => baseScenes.includes(scene))) {
		throw new Invalid(`the base build has none of ${settings.scenes.join(', ')}, so there is nothing to compare`);
	}

	const verdicts = [];
	const scenes = {};
	for (const scene of settings.scenes) {
		if (!baseScenes.includes(scene)) {
			// New in the head: its figures, nothing to gate them against.
			const headRuns = await runs(headExe, scene, settings, settings.runs, ' head (new),');
			const summary = summarise(headRuns, allKeys(scene));
			console.log(`  ${scene}: new, not in the base build; head only`);
			for (const [key, row] of Object.entries(summary)) {
				console.log(`    ${key.padEnd(16)} ${'-'.padStart(12)} ${fmt(row?.[STATISTIC]).padStart(12)}`);
			}
			scenes[scene] = { verdict: 'new', summary, notes: ['not in the base build'], base: [], head: headRuns };
			continue;
		}
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
		const { verdict, rows, notes } = judgeScene(scene, baseRuns, headRuns, settings);
		console.log(`  ${scene}: ${verdict}`);
		for (const [key, row] of Object.entries(rows)) {
			const spread = row.spreads ? `  spread ${row.spreads.base.toFixed(1)}% / ${row.spreads.head.toFixed(1)}%` : '';
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
	const captures = Object.values(scenes).flatMap((s) => [...s.base, ...s.head]);
	const result = {
		...top,
		settings: settingsOf(settings, captures),
		gates: settings.threshold === null ? GATES : `${settings.threshold}% everywhere`,
		base,
		head: { exe: headExe, source: options.exe ? null : describe(ROOT) },
		verdict: overall,
		scenes,
	};
	write(options.out ?? path.join(ROOT, 'perf-results/local', `compare-${Date.now()}.json`), result);
	console.log(`perf: ${overall}`);
	process.exit(EXIT[overall]);
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
