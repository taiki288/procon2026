#!/usr/bin/env node
// judge の出力フォルダを集計する（solver ごと・マップサイズごと）
//
//   node tools/stats.js logs/0928-2215-10teams
//
// 出す指標
//   日別      1 試合あたりの日ごとの種類の合計
//   玉        1 試合あたりの玉数
//   順位      平均順位
//   止まった  燃料切れで 1 日中動けなかった巡回車（台×日, 1 試合あたり）
"use strict";
const fs = require("fs");
const path = require("path");
const Sim = require("../visualizer/sim.js");

const dir = process.argv[2];
if (!dir) { console.error("usage: node tools/stats.js <judge の出力フォルダ>"); process.exit(1); }
const solverOf = (name) => String(name).replace(/#\d+$/, "");

const agg = {};  // key: solver|size
for (const f of fs.readdirSync(dir).filter((f) => /^\d+\.json$/.test(f)).sort()) {
  const R = Sim.run(JSON.parse(fs.readFileSync(path.join(dir, f), "utf8")));
  const size = R.pb.width;
  for (const t of R.teams) {
    let stuck = 0;
    t.days.forEach((d) => {
      const s = d.frames[0].agents, e = d.frames[d.frames.length - 1].agents;
      s.forEach((a, i) => { if (a.kind === 0 && a.pos === e[i].pos && a.fuel < 2) stuck++; });
    });
    for (const key of [`${solverOf(t.name)}|${size}`, `${solverOf(t.name)}|全体`]) {
      const A = (agg[key] ||= { n: 0, day: 0, balls: 0, rank: 0, stuck: 0, wins: 0 });
      A.n++; A.day += t.dayBrands; A.balls += t.balls; A.rank += t.rank; A.stuck += stuck; A.wins += t.rank === 1;
    }
  }
}
const keys = Object.keys(agg).sort((a, b) => {
  const [sa, za] = a.split("|"), [sb, zb] = b.split("|");
  return za.localeCompare(zb, undefined, { numeric: true }) || sa.localeCompare(sb);
});
const w = Math.max(10, ...keys.map((k) => k.split("|")[0].length));
console.log(`${"サイズ".padEnd(5)} ${"solver".padEnd(w)}  出場  1位  平均順位   日別     玉  止まった(台×日)`);
let prev = null;
for (const k of keys) {
  const [s, z] = k.split("|"), A = agg[k];
  if (prev !== null && prev !== z) console.log("");
  prev = z;
  console.log(`${String(z).padEnd(6)} ${s.padEnd(w)} ${String(A.n).padStart(5)} ${String(A.wins).padStart(4)} ${(A.rank / A.n).toFixed(2).padStart(9)}` +
    ` ${(A.day / A.n).toFixed(1).padStart(6)} ${(A.balls / A.n).toFixed(1).padStart(6)} ${(A.stuck / A.n).toFixed(2).padStart(10)}`);
}
