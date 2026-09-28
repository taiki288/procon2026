#!/usr/bin/env node
// ローカルジャッジ: サーバーを使わず、visualizer/sim.js のシミュレータで試合を回す。
// 複数の solver を同じ試合に出して、seed ごとの結果 JSON（visualizer でそのまま開ける）を書き出す。
//
//   node tools/judge.js solvers/escort.cpp solvers/hub.cpp --seeds 0-99
//   node tools/judge.js --teams hub:1,hub@v1:1,greedy:8 --seeds 0-29
//   node tools/judge.js solvers/hub.cpp --maps maps/sample_16.json,maps/sample_32.json --copies 3
//
// solver の書き方
//   solvers/hub.cpp / hub（= solvers/hub.cpp）/ build/x.exe
//   hub@v1             git のタグ（やコミット）v1 時点の solvers/ からビルドした版
//
// オプション
//   --teams a:N,b:M    チーム構成（solver ごとのチーム数。:N を省くと 1）
//   --seeds 0-99       maps/gen/NNNN.json を使う（なければ tools/gen.py で生成）
//   --maps a.json,...  マップを直接指定（--seeds の代わり）
//   --copies N         各 solver を N チームずつ出場させる（既定 1）
//   --time-ms 1000     各日に solver へ渡す時間（本番は 60 秒だが、ローカルでは短く）
//   --kind-time-ms 2000
//   --parallel N       同時に動かす solver プロセス数（既定: CPU コア数 - 1）
//   --out DIR          出力先（既定: logs/月日-時分-チーム数teams  例: logs/0928-1715-3teams）
"use strict";
const fs = require("fs");
const path = require("path");
const os = require("os");
const { spawn, execFileSync } = require("child_process");
const Sim = require("../visualizer/sim.js");

const ROOT = path.resolve(__dirname, "..");

// ---------------- 引数 ----------------
function parseArgs() {
  const a = process.argv.slice(2), opt = { solvers: [], copies: 1, timeMs: 1000, kindTimeMs: 2000,
    parallel: Math.max(1, os.cpus().length - 1), seeds: null, maps: null, out: null };
  for (let i = 0; i < a.length; i++) {
    const k = a[i], v = () => a[++i];
    if (k === "--seeds") opt.seeds = v();
    else if (k === "--teams") opt.teams = v();
    else if (k === "--maps") opt.maps = v().split(",").filter(Boolean);
    else if (k === "--copies") opt.copies = +v();
    else if (k === "--time-ms") opt.timeMs = +v();
    else if (k === "--kind-time-ms") opt.kindTimeMs = +v();
    else if (k === "--parallel") opt.parallel = +v();
    else if (k === "--out") opt.out = v();
    else if (k.startsWith("--")) { console.error("不明なオプション " + k); process.exit(1); }
    else opt.solvers.push(k);
  }
  if (!opt.solvers.length && !opt.teams) { console.error("usage: node tools/judge.js <solver> ... | --teams a:N,b:M  [--seeds 0-99]"); process.exit(1); }
  if (!opt.seeds && !opt.maps) opt.seeds = "0-9";
  return opt;
}
function parseSeeds(s) {
  const out = [];
  for (const part of s.split(",")) {
    const m = part.split("-").map(Number);
    if (m.length === 2) for (let x = m[0]; x <= m[1]; x++) out.push(x); else out.push(m[0]);
  }
  return out;
}

// ---------------- ビルド ----------------
function build(src, exeName) {
  if (src.endsWith(".exe")) return path.resolve(src);
  const name = exeName || path.basename(src, ".cpp");
  const exe = path.join(ROOT, "build", name + ".exe");
  fs.mkdirSync(path.dirname(exe), { recursive: true });
  const dir = path.dirname(path.resolve(src));
  const deps = [src, ...fs.readdirSync(dir).filter((f) => f.endsWith(".hpp")).map((f) => path.join(dir, f))];
  const newest = Math.max(...deps.map((d) => fs.statSync(d).mtimeMs));
  if (!fs.existsSync(exe) || newest > fs.statSync(exe).mtimeMs) {
    console.log(`build ${src} -> ${path.relative(ROOT, exe)}`);
    execFileSync("g++", ["-std=c++17", "-O2", "-o", exe, src], { stdio: "inherit" });
  }
  return exe;
}

// "hub" / "solvers/hub.cpp" / "x.exe" / "hub@v1" → { label, exe }
function resolveSolver(spec) {
  const m = spec.match(/^(.+?)@([^@]+)$/);
  const base = m ? m[1] : spec, rev = m ? m[2] : null;
  if (base.endsWith(".exe")) {
    if (rev) throw new Error(".exe にはタグを指定できません: " + spec);
    return { label: path.basename(base, ".exe"), exe: path.resolve(base) };
  }
  const src = base.endsWith(".cpp") ? base : path.join("solvers", base + ".cpp");
  const name = path.basename(src, ".cpp");
  if (!rev) return { label: name, exe: build(src) };
  // git の版: 同じフォルダのファイルをその版の内容で build/src/<版>/ に書き出してビルド
  const rel = path.relative(ROOT, path.resolve(src)).split(path.sep).join("/"), dir = path.posix.dirname(rel);
  const git = (...args) => execFileSync("git", args, { cwd: ROOT, encoding: "utf8" });
  const outDir = path.join(ROOT, "build", "src", rev.replace(/[^\w.-]/g, "_"));
  const exeName = `${name}@${rev.replace(/[^\w.-]/g, "_")}`;
  if (!fs.existsSync(path.join(ROOT, "build", exeName + ".exe"))) {
    fs.mkdirSync(path.join(outDir, dir), { recursive: true });
    for (const f of git("ls-tree", "--name-only", rev, dir + "/").split("\n").filter(Boolean))
      fs.writeFileSync(path.join(outDir, f), execFileSync("git", ["show", `${rev}:${f}`], { cwd: ROOT }));
  }
  return { label: `${name}@${rev}`, exe: build(path.join(outDir, rel), exeName) };
}

// ---------------- マップ ----------------
function toProblem(obj) {
  if (obj.problem) return obj.problem;
  return { width: obj.map.width, height: obj.map.height, cells: obj.map.cells, spots: obj.spots, agentStarts: obj.agents,
    fuelLimits: obj.fuelLimits, daySteps: obj.daySteps, daySeconds: obj.daySeconds,
    busyThreshold: obj.busyThreshold, jammedThreshold: obj.jammedThreshold };
}
function mapList(opt) {
  if (opt.maps) return opt.maps.map((m) => ({ name: path.basename(m, ".json"), file: path.resolve(m) }));
  const seeds = parseSeeds(opt.seeds), dir = path.join(ROOT, "maps", "gen");
  const missing = seeds.filter((s) => !fs.existsSync(path.join(dir, String(s).padStart(4, "0") + ".json")));
  if (missing.length) execFileSync("python", [path.join(ROOT, "tools", "gen.py"), missing.join(",")], { stdio: "inherit" });
  return seeds.map((s) => ({ name: String(s).padStart(4, "0"), file: path.join(dir, String(s).padStart(4, "0") + ".json") }));
}

// ---------------- solver 入力（tools/procon_io.py の build_input と同じ書式） ----------------
function header(pb, players, mode, timeMs) {
  const L = [`${pb.height} ${pb.width}`, ...pb.cells.map((r) => r.join(" ")), String(pb.spots.length),
    ...pb.spots.map((s) => `${s.brand} ${s.pos} ${s.stocks}`), String(pb.agentStarts.length), pb.agentStarts.join(" "),
    String(pb.fuelLimits), String(pb.daySteps.length), pb.daySteps.join(" "), pb.daySeconds.join(" "),
    `${players} ${pb.busyThreshold} ${pb.jammedThreshold}`, mode, String(Math.round(timeMs))];
  return L;
}
const trafficLines = (list) => [String(list.length), ...list.map((t) => `${t.pos} ${t.status}`)];
const agentLines = (ags) => ags.map((a) => `${a.kind} ${a.pos} ${a.fuel}`);

// ---------------- プロセス実行（同時実行数を制限） ----------------
let running = 0;
const queue = [];
function acquire() { return new Promise((res) => { if (running < OPT.parallel) { running++; res(); } else queue.push(res); }); }
function release() { running--; if (queue.length) { running++; queue.shift()(); } }
async function runSolver(exe, input, timeoutMs) {
  await acquire();
  return new Promise((resolve) => {
    const t0 = Date.now();
    const p = spawn(exe, [], { stdio: ["pipe", "pipe", "pipe"] });
    let out = "", err = "", done = false;
    const timer = setTimeout(() => { if (!done) { err += "\n[judge] タイムアウト"; p.kill(); } }, timeoutMs);
    p.stdout.on("data", (d) => (out += d));
    p.stderr.on("data", (d) => (err += d));
    p.on("close", () => { done = true; clearTimeout(timer); release(); resolve({ out, err, ms: Date.now() - t0 }); });
    p.on("error", (e) => { err += String(e); });
    p.stdin.on("error", () => {});
    p.stdin.end(input);
  });
}

// ---------------- 1 試合 ----------------
async function playMatch(map, teams) {
  const pb = toProblem(JSON.parse(fs.readFileSync(map.file, "utf8")));
  const geo = Sim.makeGeo(pb), NA = pb.agentStarts.length, D = pb.daySteps.length, players = teams.length;
  const logs = teams.map(() => "");
  const res = teams.map((t) => ({ name: t.name, kinds: null, actions: [], times: [] }));

  // KIND
  const kinds = await Promise.all(teams.map((t) =>
    runSolver(t.exe, header(pb, players, "KIND", OPT.kindTimeMs).join("\n") + "\n", OPT.kindTimeMs + 3000)));
  kinds.forEach((r, i) => {
    logs[i] += `=== KIND (${r.ms}ms)\n${r.err}`;
    const v = (r.out.trim().split(/\s+/) || []).map(Number);
    res[i].kinds = v.length === NA && v.every((k) => k === 0 || k === 1) ? v : new Array(NA).fill(0);
  });
  let state = res.map((r) => pb.agentStarts.map((p, i) => ({ kind: r.kinds[i], pos: p, fuel: pb.fuelLimits })));
  const stays = [], history = teams.map(() => []);

  for (let d = 0; d < D; d++) {
    const sum = new Array(geo.NC).fill(0);
    for (let k = Math.max(0, d - 2); k < d; k++) for (let p = 0; p < geo.NC; p++) sum[p] += stays[k][p];
    const status = Sim.statusFromStay(pb, geo, sum, players);
    const traffics = [];
    for (let p = 0; p < geo.NC; p++) if (geo.cells[p] === Sim.ROAD) traffics.push({ pos: p, status: status[p] });
    const steps = pb.daySteps[d];

    const outs = await Promise.all(teams.map((t, ti) => {
      const L = header(pb, players, "DAY", OPT.timeMs);
      L.push(String(d), ...agentLines(state[ti]), ...trafficLines(traffics), String(players - 1));
      teams.forEach((_, oj) => { if (oj !== ti) L.push(String(oj), ...agentLines(state[oj])); });
      for (const h of history[ti]) L.push(...trafficLines(h.traffics), ...h.plan.map((p) => [p.length, ...p].join(" ")));
      return runSolver(t.exe, L.join("\n") + "\n", OPT.timeMs + 3000);
    }));

    const dayStay = new Array(geo.NC).fill(0);
    outs.forEach((r, ti) => {
      logs[ti] += `=== DAY ${d} (${r.ms}ms)\n${r.err}`;
      const plan = r.out.split("\n").filter((l) => l.trim()).map((l) => l.trim().split(/\s+/).map(Number));
      let sim = Sim.simulateDay(pb, geo, state[ti], plan, status, steps);
      let adopted = plan;
      if (!sim.valid) {
        logs[ti] += `[judge] 無効な回答: ${sim.error}\n`;
        adopted = state[ti].map(() => [-steps]);
        sim = Sim.simulateDay(pb, geo, state[ti], adopted, status, steps);
      }
      res[ti].actions.push(plan);  // 無効でも記録（visualizer がエラーとして表示する）
      res[ti].times.push(r.ms / 1000);
      history[ti].push({ traffics, plan: adopted });
      for (let p = 0; p < geo.NC; p++) dayStay[p] += sim.stay[p];
      state[ti] = sim.end;
    });
    stays.push(dayStay);
  }
  return { log: { seed: map.name, problem: pb, players, teams: res }, stderr: logs };
}

// 出力先の既定: logs/月日-時分-チーム数teams（同じ名前があれば -2, -3 ...）
function defaultOutDir(nTeams) {
  const d = new Date(), z = (x) => String(x).padStart(2, "0");
  const base = path.join(ROOT, "logs", `${z(d.getMonth() + 1)}${z(d.getDate())}-${z(d.getHours())}${z(d.getMinutes())}-${nTeams}teams`);
  let out = base;
  for (let k = 2; fs.existsSync(out); k++) out = `${base}-${k}`;
  return out;
}

// ---------------- main ----------------
const OPT = parseArgs();
(async () => {
  // チーム構成: 並べた solver（各 --copies チーム）+ --teams a:N,b:M
  const entries = OPT.solvers.map((s) => [s, OPT.copies]);
  if (OPT.teams) for (const e of OPT.teams.split(",").filter(Boolean)) {
    const m = e.match(/^(.*?)(?::(\d+))?$/);
    entries.push([m[1], m[2] ? +m[2] : 1]);
  }
  const teams = [];
  for (const [spec, n] of entries) {
    const { label, exe } = resolveSolver(spec);
    const total = entries.filter(([s]) => s === spec).reduce((x, [, c]) => x + c, 0);
    for (let c = 0; c < n; c++) {
      const k = teams.filter((t) => t.solver === label).length;
      teams.push({ name: total > 1 ? `${label}#${k}` : label, solver: label, exe });
    }
  }
  const maps = mapList(OPT);
  const out = path.resolve(OPT.out || defaultOutDir(teams.length));
  fs.mkdirSync(path.join(out, "stderr"), { recursive: true });
  console.log(`${maps.length} 試合 × ${teams.length} チーム  (time ${OPT.timeMs}ms/日, 並列 ${OPT.parallel})  → ${path.relative(ROOT, out)}`);

  const summary = [];
  let finished = 0;
  const t0 = Date.now();
  await Promise.all(maps.map(async (m) => {
    const { log, stderr } = await playMatch(m, teams);
    fs.writeFileSync(path.join(out, m.name + ".json"), JSON.stringify(log));
    stderr.forEach((s, i) => fs.writeFileSync(path.join(out, "stderr", `${m.name}-${teams[i].name}.txt`), s));
    const R = Sim.run(log);
    // 渋滞: 日ごとの 混雑 / 渋滞 した道路セル数
    const roads = R.geo.cells.filter((c) => c === Sim.ROAD).length;
    const traffic = R.statuses.map((st) => ({ busy: st.filter((x) => x === 1).length, jammed: st.filter((x) => x === 2).length }));
    summary.push({ seed: m.name, roads, traffic, teams: R.teams.map((t, i) => ({ name: t.name, solver: teams[i].solver, rank: t.rank,
      matchBrands: t.matchBrands, dayBrands: t.dayBrands, balls: t.balls, invalid: t.invalid })) });
    finished++;
    process.stdout.write(`\r  ${finished}/${maps.length} 試合完了 (${((Date.now() - t0) / 1000).toFixed(0)}s)`);
  }));
  console.log();
  summary.sort((a, b) => a.seed.localeCompare(b.seed));
  fs.writeFileSync(path.join(out, "summary.json"), JSON.stringify(summary, null, 1));

  // 集計（solver ごと）
  const agg = {};
  for (const s of summary) for (const t of s.teams) {
    const A = (agg[t.solver] ||= { games: 0, wins: 0, rank: 0, matchBrands: 0, dayBrands: 0, balls: 0, invalid: 0 });
    A.games++; A.wins += t.rank === 1; A.rank += t.rank;
    A.matchBrands += t.matchBrands; A.dayBrands += t.dayBrands; A.balls += t.balls; A.invalid += t.invalid;
  }
  const w = Math.max(12, ...Object.keys(agg).map((n) => n.length));
  console.log(`${"solver".padEnd(w)}  出場  1位  平均順位  総種類計  日別計   玉計  1試合平均(日別/玉)  無効日`);
  for (const [n, A] of Object.entries(agg))
    console.log(`${n.padEnd(w)} ${String(A.games).padStart(5)} ${String(A.wins).padStart(4)} ${(A.rank / A.games).toFixed(2).padStart(9)}` +
      ` ${String(A.matchBrands).padStart(9)} ${String(A.dayBrands).padStart(7)} ${String(A.balls).padStart(6)}` +
      ` ${(A.dayBrands / A.games).toFixed(1).padStart(12)} / ${(A.balls / A.games).toFixed(1).padEnd(6)} ${String(A.invalid).padStart(5)}`);
  // 渋滞の割合（全試合・全日の平均。1 日目は必ず全部順調なので除く）
  let busy = 0, jam = 0, cnt = 0;
  for (const s of summary) s.traffic.slice(1).forEach((t) => { busy += t.busy / s.roads; jam += t.jammed / s.roads; cnt++; });
  if (cnt) console.log(`\n道路の状態（2 日目以降の平均）: 混雑 ${(100 * busy / cnt).toFixed(1)}%  渋滞 ${(100 * jam / cnt).toFixed(1)}%`);
  console.log(`\nvisualizer で「フォルダを開く」→ ${out}`);
})();
