#!/usr/bin/env python3
"""
競技サーバーとの通信担当。solver(build/hub.exe など) を呼び出して回答を提出し、ログを保存する。
標準ライブラリのみ。

  python tools/client.py --url http://localhost:8080 --token token-p0 --solver build/hub.exe

流れ
  1. GET /setting を取れるまで待つ
  2. solver(KIND) → POST /agent
  3. GET / をポーリング。日が変わるたびに
       a. 全員待機（必ず有効）をすぐ提出（保険）
       b. solver(DAY) → POST /
  4. ログ(visualizer でそのまま開ける形式)を毎日保存
"""
import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procon_io import build_input, parse_kinds, parse_plan, setting_to_problem  # noqa: E402


class Api:
    MIN_INTERVAL = 0.2  # 秒間 5 リクエストまで

    def __init__(self, url, token):
        self.url = url.rstrip("/")
        self.token = token
        self.last = 0.0

    def request(self, method, path, body=None):
        wait = self.last + self.MIN_INTERVAL - time.time()
        if wait > 0:
            time.sleep(wait)
        self.last = time.time()
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.url + path, data=data, method=method)
        req.add_header("Procon-Token", self.token)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=5) as res:
                raw = res.read()
                return res.status, (json.loads(raw) if raw.strip() else None)
        except urllib.error.HTTPError as e:
            raw = e.read()
            try:
                return e.code, json.loads(raw)
            except ValueError:
                return e.code, raw.decode(errors="replace")
        except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
            return None, str(e)


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def run_solver(solver, text, timeout_s, stderr):
    try:
        p = subprocess.run([solver], input=text, capture_output=True, text=True, timeout=timeout_s)
        if stderr:
            stderr.write(p.stderr)
            stderr.flush()
        else:
            sys.stderr.write(p.stderr)
        return p.stdout
    except subprocess.TimeoutExpired:
        log("solver がタイムアウト")
        return ""


def main():
    if not sys.stdout.isatty():  # ファイルへリダイレクトされたときは UTF-8 で書く
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://localhost:8080")
    ap.add_argument("--token", required=True)
    ap.add_argument("--solver", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "hub.exe"))
    ap.add_argument("--log", default=None, help="ログ保存先（既定: logs/<token>.json）")
    ap.add_argument("--solver-log", default=None, help="solver の標準エラー出力の保存先")
    ap.add_argument("--kind-time-ms", type=int, default=3000, help="KIND で solver に渡す時間")
    ap.add_argument("--margin", type=float, default=3.0, help="締切前に残す秒数")
    ap.add_argument("--poll", type=float, default=0.5)
    ap.add_argument("--name", default=None)
    args = ap.parse_args()

    api = Api(args.url, args.token)
    log_path = args.log or os.path.join("logs", f"{args.token}.json")
    os.makedirs(os.path.dirname(log_path) or ".", exist_ok=True)
    solver_err = open(args.solver_log, "w", encoding="utf-8") if args.solver_log else None

    # ---- 1. 試合設定 ----
    while True:
        code, setting = api.request("GET", "/setting")
        if code == 200:
            break
        if code == 401:
            log(f"認証エラー: {setting}")
            return 1
        time.sleep(args.poll)
    problem = setting_to_problem(setting)
    players = setting["players"]
    na = len(setting["agents"])
    D = len(setting["daySteps"])
    log(f"試合設定を取得: {problem['width']}x{problem['height']} agents={na} days={D} players={players}")

    team = {"name": args.name or args.token, "kinds": None, "actions": [], "times": [], "serverStates": []}
    out = {"problem": problem, "players": players, "teams": [team], "traffics": []}

    def save():
        with open(log_path, "w", encoding="utf-8") as f:
            json.dump(out, f, ensure_ascii=False)

    # ---- 2. エージェント種別 ----
    text = build_input(problem, players, "KIND", args.kind_time_ms)
    kinds = parse_kinds(run_solver(args.solver, text, args.kind_time_ms / 1000 + 5, solver_err), na)
    if kinds is None:
        log("KIND の出力が不正 → 全員巡回車")
        kinds = [0] * na
    code, res = api.request("POST", "/agent", kinds)
    log(f"種別を提出 {kinds} → {code} {res if code != 200 else ''}")
    team["kinds"] = kinds
    save()

    # ---- 3. 各日 ----
    history = []
    handled = -1
    while True:
        code, st = api.request("GET", "/")
        now = time.time()
        if code != 200:
            if handled >= D - 1:
                break
            time.sleep(args.poll)
            continue
        day = st["day"]
        if day >= D:
            break
        if day == handled or st["endsAt"] <= now:
            if handled >= D - 1 and st["endsAt"] <= now:
                break
            time.sleep(args.poll)
            continue

        steps = setting["daySteps"][day]
        day_start = st["endsAt"] - setting["daySeconds"][day]
        team["kinds"] = [a["kind"] for a in st["agents"]]
        team["serverStates"].append({"day": day, "agents": st["agents"], "others": st["others"]})
        out["traffics"].append(st["traffics"])

        # a. 保険
        adopted, adopted_at = None, None
        fallback = [[-steps] for _ in range(na)]
        code, res = api.request("POST", "/", fallback)
        if code == 200 and res and res.get("revision", -1) >= 0:
            adopted, adopted_at = fallback, time.time()
        else:
            log(f"day {day}: 保険の提出に失敗 {code} {res}")

        # b. solver
        budget = st["endsAt"] - time.time() - args.margin
        if budget > 0.5:
            text = build_input(problem, players, "DAY", budget * 1000, day, st, history)
            plan = parse_plan(run_solver(args.solver, text, budget + 1, solver_err), na)
            if plan is None:
                log(f"day {day}: solver の出力が不正")
            else:
                code, res = api.request("POST", "/", plan)
                rev = res.get("revision", -1) if code == 200 and isinstance(res, dict) else -1
                if rev >= 0:
                    adopted, adopted_at = plan, time.time()
                log(f"day {day}: 提出 → {code} revision={rev}")
        if adopted is None:
            adopted = fallback
        team["actions"].append(adopted)
        team["times"].append(round(adopted_at - day_start, 3) if adopted_at else None)
        history.append({"traffics": st["traffics"], "plan": adopted})
        handled = day
        save()

    log(f"試合終了。ログ: {log_path}")
    save()
    return 0


if __name__ == "__main__":
    sys.exit(main())
