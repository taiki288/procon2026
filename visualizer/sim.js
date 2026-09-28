// ヘキサうどん シミュレータ（visualizer / スコア計算 共通）
//   ブラウザ: <script src="sim.js"> → window.Sim
//   CLI     : node visualizer/sim.js logs/xxx/vis.json   （スコア表と検証結果を表示）
(function (root) {
  "use strict";
  const PLAIN = 0, ROAD = 1, MOUNTAIN = 2, POND = 3;
  const DX_EVEN = [0, 1, 1, 1, 0, -1], DX_ODD = [-1, 0, 1, 0, -1, -1], DY = [-1, -1, 0, 1, 1, 0];

  function makeGeo(pb) {
    const W = pb.width, H = pb.height, cells = pb.cells.flat();
    const neighbor = (p, d) => {
      const x = p % W, y = Math.floor(p / W);
      const nx = x + (y % 2 === 0 ? DX_EVEN[d] : DX_ODD[d]), ny = y + DY[d];
      if (nx < 0 || nx >= W || ny < 0 || ny >= H) return -1;
      return ny * W + nx;
    };
    const stepCost = (p, st) => cells[p] === PLAIN ? 2 : cells[p] === MOUNTAIN ? 3 : [1, 2, 4][st[p] || 0];
    const fuelCost = (p) => cells[p] === PLAIN ? 1 : 2;
    const spotAt = new Array(W * H).fill(-1);
    pb.spots.forEach((s, i) => (spotAt[s.pos] = i));
    return { W, H, NC: W * H, cells, neighbor, stepCost, fuelCost, spotAt };
  }

  // 1 日分。frames[t] = ステップ t の反映後の状態（t=0 は開始時）
  function simulateDay(pb, geo, start, plan, status, steps) {
    const n = start.length, S = pb.spots.length;
    const fail = (msg) => ({ valid: false, error: msg });
    if (!Array.isArray(plan) || plan.length !== n) return fail("エージェント数が違う");
    for (let i = 0; i < n; i++) {
      let cur = start[i].pos, tot = 0;
      if (!Array.isArray(plan[i]) || plan[i].length === 0) return fail(`agent ${i}: 行動が空`);
      for (const a of plan[i]) {
        if (!Number.isInteger(a) || a > 5) return fail(`agent ${i}: 不正な値 ${a}`);
        if (a < 0) tot += -a;
        else {
          const nx = geo.neighbor(cur, a);
          if (nx < 0 || geo.cells[nx] === POND) return fail(`agent ${i}: 移動不可能なセルへ移動`);
          tot += geo.stepCost(cur, status);
          cur = nx;
        }
      }
      if (tot !== steps) return fail(`agent ${i}: ステップ数 ${tot} ≠ ${steps}`);
    }
    const a = start.map((x) => ({ ...x }));
    const ptr = new Array(n).fill(0), doneAt = new Array(n).fill(Infinity), target = new Array(n).fill(-1), fc = new Array(n).fill(0);
    const stock = pb.spots.map((s) => s.stocks);
    const got = a.map(() => new Array(S).fill(false));
    const stay = new Array(geo.NC).fill(0);
    const gains = a.map(() => 0);
    const brands = new Set();
    const events = [];
    const snap = (t) => ({ t, agents: a.map((x) => ({ ...x })), stock: stock.slice(), gains: gains.slice(), brands: brands.size });
    const startAction = (i, t) => {
      if (ptr[i] >= plan[i].length) { doneAt[i] = Infinity; target[i] = -1; return; }
      const x = plan[i][ptr[i]++];
      if (x < 0) { doneAt[i] = t - x; target[i] = -1; }
      else { target[i] = geo.neighbor(a[i].pos, x); fc[i] = geo.fuelCost(a[i].pos); doneAt[i] = t + geo.stepCost(a[i].pos, status); }
    };
    for (let i = 0; i < n; i++) startAction(i, 0);
    const frames = [snap(0)];
    for (let t = 1; t <= steps; t++) {
      for (let i = 0; i < n; i++)
        if (doneAt[i] === t && target[i] >= 0 && a[i].kind === 0) {
          if (a[i].fuel < fc[i]) return fail(`agent ${i}: step ${t} で燃料不足`);
          a[i].fuel -= fc[i];
        }
      for (let i = 0; i < n; i++) if (doneAt[i] === t && target[i] >= 0) a[i].pos = target[i];
      for (let i = 0; i < n; i++) {
        if (a[i].kind !== 0) continue;
        const s = geo.spotAt[a[i].pos];
        if (s >= 0 && !got[i][s] && stock[s] > 0) {
          got[i][s] = true; stock[s]--; gains[i]++; brands.add(pb.spots[s].brand);
          events.push({ t, agent: i, spot: s, brand: pb.spots[s].brand });
        }
      }
      for (let i = 0; i < n; i++) {
        if (a[i].kind !== 0) continue;
        if (a.some((b) => b.kind === 1 && b.pos === a[i].pos)) a[i].fuel = pb.fuelLimits;
      }
      for (let i = 0; i < n; i++) if (geo.cells[a[i].pos] === ROAD) stay[a[i].pos]++;
      if (t < steps) for (let i = 0; i < n; i++) if (doneAt[i] === t) startAction(i, t);
      frames.push(snap(t));
    }
    return { valid: true, frames, stay, brands: [...brands], balls: gains.reduce((x, y) => x + y, 0), events, end: a };
  }

  function statusFromStay(pb, geo, sum, players) {
    const st = new Array(geo.NC).fill(0);
    for (let p = 0; p < geo.NC; p++) {
      if (geo.cells[p] !== ROAD) continue;
      st[p] = sum[p] < pb.busyThreshold * players ? 0 : sum[p] < pb.jammedThreshold * players ? 1 : 2;
    }
    return st;
  }
  function statusFromList(geo, list) {
    const st = new Array(geo.NC).fill(0);
    for (const x of list) st[x.pos] = x.status;
    return st;
  }

  // ログ全体を再生する
  //   log = { problem, players?, teams: [{ name, kinds, actions: [day][agent][...], times?, serverStates?, serverTraffics? }] }
  function run(log, opts = {}) {
    const pb = log.problem, geo = makeGeo(pb), D = pb.daySteps.length, NA = pb.agentStarts.length;
    const teams = log.teams || [];
    const players = log.players || teams.length || 1;
    // 自チームのログしかない（本番）ときは、サーバーが配った道路状態を使う
    const useServer = opts.useServerTraffic ?? (teams.length < players && log.traffics);
    const state = teams.map((tm) => pb.agentStarts.map((p, i) => ({ kind: (tm.kinds || [])[i] || 0, pos: p, fuel: pb.fuelLimits })));
    const res = teams.map((tm) => ({ name: tm.name, kinds: tm.kinds, days: [], collected: new Set(), dayBrands: 0, balls: 0, invalid: 0,
      times: tm.times || [], time: (tm.times || []).reduce((x, y) => x + (y || 0), 0), checks: [] }));
    const stays = [], statuses = [];
    for (let d = 0; d < D; d++) {
      let status;
      if (useServer && log.traffics[d]) status = statusFromList(geo, log.traffics[d]);
      else {
        const sum = new Array(geo.NC).fill(0);
        for (let k = Math.max(0, d - 2); k < d; k++) for (let p = 0; p < geo.NC; p++) sum[p] += stays[k][p];
        status = statusFromStay(pb, geo, sum, players);
      }
      statuses.push(status);
      const dayStay = new Array(geo.NC).fill(0);
      teams.forEach((tm, ti) => {
        const R = res[ti], steps = pb.daySteps[d];
        // サーバーの状態との照合
        const ss = (tm.serverStates || []).find((x) => x.day === d);
        if (ss) ss.agents.forEach((sa, i) => {
          const me = state[ti][i];
          if (sa.pos !== me.pos || (me.kind === 0 && sa.fuel !== me.fuel))
            R.checks.push(`day ${d} agent ${i}: sim(pos=${me.pos},fuel=${me.fuel}) server(pos=${sa.pos},fuel=${sa.fuel})`);
        });
        const st = (tm.serverTraffics || [])[d];
        if (st && !useServer) {
          const bad = st.filter((x) => status[x.pos] !== x.status).length;
          if (bad) R.checks.push(`day ${d}: 道路状態がサーバーと ${bad} セル違う`);
        }
        const plan = (tm.actions || [])[d];
        let r = plan ? simulateDay(pb, geo, state[ti], plan, status, steps) : null;
        let error = plan ? null : "回答なし";
        if (r && !r.valid) error = r.error;
        if (!r || !r.valid) {
          if (d < (tm.actions || []).length) R.invalid++;
          r = simulateDay(pb, geo, state[ti], state[ti].map(() => [-steps]), status, steps);
        }
        r.error = error;
        r.plan = plan;
        r.brands.forEach((b) => R.collected.add(b));
        R.dayBrands += r.brands.length;
        R.balls += r.balls;
        R.days.push(r);
        for (let p = 0; p < geo.NC; p++) dayStay[p] += r.stay[p];
        state[ti] = r.end;
      });
      stays.push(dayStay);
    }
    res.forEach((R) => (R.matchBrands = R.collected.size));
    const order = res.map((_, i) => i).sort((i, j) => {
      const a = res[i], b = res[j];
      return b.matchBrands - a.matchBrands || b.dayBrands - a.dayBrands || b.balls - a.balls || a.time - b.time;
    });
    order.forEach((ti, k) => (res[ti].rank = k + 1));
    const numBrands = new Set(pb.spots.map((s) => s.brand)).size;
    return { pb, geo, D, NA, players, teams: res, statuses, stays, numBrands, useServer: !!useServer };
  }

  const Sim = { run, makeGeo, simulateDay, statusFromStay, PLAIN, ROAD, MOUNTAIN, POND };
  if (typeof module !== "undefined" && module.exports) module.exports = Sim;
  else root.Sim = Sim;

  // ---- CLI ----
  if (typeof require !== "undefined" && typeof module !== "undefined" && require.main === module) {
    const fs = require("fs");
    const args = process.argv.slice(2), asJson = args.includes("--json");
    const path = args.find((a) => !a.startsWith("--"));
    if (!path) { console.error("usage: node sim.js <log.json> [--json]"); process.exit(1); }
    const R = run(JSON.parse(fs.readFileSync(path, "utf8")));
    if (asJson) {
      console.log(JSON.stringify(R.teams.map((t) => ({ name: t.name, rank: t.rank, matchBrands: t.matchBrands, dayBrands: t.dayBrands,
        balls: t.balls, time: t.time, invalid: t.invalid, checks: t.checks.length, numBrands: R.numBrands }))));
      process.exit(0);
    }
    console.log(`系列数 ${R.numBrands} / ${R.D} 日 / ${R.players} チーム`);
    console.log("順位  総種類  日別種類計  玉数  回答時間  無効日  チーム");
    [...R.teams].sort((a, b) => a.rank - b.rank).forEach((t) =>
      console.log(`${String(t.rank).padStart(4)}  ${String(t.matchBrands).padStart(6)}  ${String(t.dayBrands).padStart(10)}  ${String(t.balls).padStart(4)}  ${t.time.toFixed(1).padStart(8)}  ${String(t.invalid).padStart(6)}  ${t.name}`));
    R.teams.forEach((t) => {
      t.days.forEach((d, i) => d.error && console.log(`  [${t.name}] day ${i}: ${d.error}`));
      if (t.checks.length) { console.log(`  [${t.name}] サーバーとの不一致:`); t.checks.forEach((c) => console.log("    " + c)); }
    });
  }
})(typeof window !== "undefined" ? window : globalThis);
