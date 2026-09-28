#!/usr/bin/env python3
"""
サーバーなしで solver を試すための入力を作る（KIND、または 1 日目の DAY）。

  python tools/make_input.py maps/sample_16.json KIND > in.txt
  python tools/make_input.py maps/sample_16.json DAY --kinds 0,0,1,0 > in.txt
  build/hub.exe < in.txt
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from procon_io import build_input, load_problem  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("mode", choices=["KIND", "DAY"])
    ap.add_argument("--time-ms", type=int, default=2000)
    ap.add_argument("--players", type=int, default=None)
    ap.add_argument("--kinds", default=None, help="DAY 用。カンマ区切り（既定: 全員巡回車）")
    args = ap.parse_args()
    with open(args.map, encoding="utf-8") as f:
        obj = json.load(f)
    problem = load_problem(obj)
    players = args.players or obj.get("players") or len(obj.get("teams", [])) or 1
    state = None
    if args.mode == "DAY":
        starts = problem["agentStarts"]
        kinds = [int(x) for x in args.kinds.split(",")] if args.kinds else [0] * len(starts)
        roads = [{"pos": y * problem["width"] + x, "status": 0}
                 for y, row in enumerate(problem["cells"]) for x, c in enumerate(row) if c == 1]
        state = {"agents": [{"kind": k, "pos": p, "fuel": problem["fuelLimits"]} for k, p in zip(kinds, starts)],
                 "others": [], "traffics": roads}
    sys.stdout.write(build_input(problem, players, args.mode, args.time_ms, 0, state, []))


if __name__ == "__main__":
    main()
