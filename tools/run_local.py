#!/usr/bin/env python3
"""
公式の簡易サーバーを立ち上げ、複数チームの client を同時に動かしてローカル対戦する。
終わると logs/月日-時分-チーム数teams/vis.json（全チーム分をまとめたもの）を作り、スコアを表示する。

  python tools/run_local.py --map maps/sample_16.json --teams 4
  python tools/run_local.py --map maps/sample_16.json --solvers build/hub.exe,build/escort.exe --day-seconds 5

--map には MatchSetting（参考マップ）と試合設定 config のどちらも渡せる。
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from procon_io import load_problem  # noqa: E402


def default_out_dir(n_teams):
    """出力先の既定: logs/月日-時分-チーム数teams（同じ名前があれば -2, -3 ...）"""
    base = os.path.join(ROOT, "logs", time.strftime("%m%d-%H%M") + f"-{n_teams}teams")
    out, k = base, 2
    while os.path.exists(out):
        out, k = f"{base}-{k}", k + 1
    return out


def main():
    if not sys.stdout.isatty():
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", required=True)
    ap.add_argument("--teams", type=int, default=2, help="チーム数（--solvers を指定したらその数）")
    ap.add_argument("--solvers", default=None, help="チームごとの solver をカンマ区切りで")
    ap.add_argument("--server", default=os.path.join(ROOT, "server", "procon-server-windows-amd64.exe"))
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--day-seconds", type=int, default=None, help="各日の回答時間を上書き（短くすると速く回せる）")
    ap.add_argument("--kind-seconds", type=int, default=5, help="種別の締切（サーバー起動から）")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    solvers = args.solvers.split(",") if args.solvers else [os.path.join(ROOT, "build", "hub.exe")] * args.teams
    n = len(solvers)
    with open(args.map, encoding="utf-8") as f:
        problem = dict(load_problem(json.load(f)))
    if args.day_seconds:
        problem["daySeconds"] = [args.day_seconds] * len(problem["daySteps"])

    out = args.out or default_out_dir(n)
    os.makedirs(out, exist_ok=True)
    tokens = [f"token-p{i}" for i in range(n)]
    config = {"problem": problem, "teams": [{"name": f"team{i}", "token": t} for i, t in enumerate(tokens)]}
    cfg_path = os.path.join(out, "config.json")
    with open(cfg_path, "w", encoding="utf-8") as f:
        json.dump(config, f)

    server_log = open(os.path.join(out, "server.log"), "w", encoding="utf-8")
    server = subprocess.Popen(
        [args.server, "-config", cfg_path, "-addr", f":{args.port}",
         "-kind-deadline", f"{args.kind_seconds}s", "-match-start-delay", "1s"],
        stdout=server_log, stderr=subprocess.STDOUT)
    time.sleep(0.5)

    clients = []
    kind_ms = max(500, args.kind_seconds * 1000 - 2000)
    for i, (tok, sol) in enumerate(zip(tokens, solvers)):
        cmd = [sys.executable, os.path.join(ROOT, "tools", "client.py"),
               "--url", f"http://localhost:{args.port}", "--token", tok, "--solver", sol,
               "--name", f"team{i}:{os.path.basename(sol)}",
               "--log", os.path.join(out, f"team{i}.json"),
               "--solver-log", os.path.join(out, f"team{i}.solver.log"),
               "--kind-time-ms", str(kind_ms), "--margin", "1.5"]
        clients.append(subprocess.Popen(cmd, stdout=open(os.path.join(out, f"team{i}.client.log"), "w", encoding="utf-8"),
                                        stderr=subprocess.STDOUT))
    total = args.kind_seconds + sum(problem["daySeconds"]) + 30
    print(f"対戦中... {n} チーム, 最大 {total} 秒  (ログ: {out})", flush=True)
    try:
        for c in clients:
            c.wait(timeout=total)
    except subprocess.TimeoutExpired:
        print("client がタイムアウトしました")
        for c in clients:
            c.kill()
    finally:
        server.terminate()

    # ---- ログをまとめる ----
    teams, states = [], []
    for i in range(n):
        p = os.path.join(out, f"team{i}.json")
        if not os.path.exists(p):
            print(f"team{i} のログがありません（{out}/team{i}.client.log を確認）")
            continue
        with open(p, encoding="utf-8") as f:
            lg = json.load(f)
        t = lg["teams"][0]
        t["serverTraffics"] = lg.get("traffics", [])
        teams.append(t)
    vis = {"problem": problem, "players": n, "teams": teams}
    vis_path = os.path.join(out, "vis.json")
    with open(vis_path, "w", encoding="utf-8") as f:
        json.dump(vis, f, ensure_ascii=False)
    print(f"visualizer 用: {vis_path}")

    node = shutil.which("node")
    if node:
        subprocess.run([node, os.path.join(ROOT, "visualizer", "sim.js"), vis_path])
    else:
        print("node が見つからないのでスコア表示を省略（visualizer で vis.json を開いてください）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
