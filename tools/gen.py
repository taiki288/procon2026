#!/usr/bin/env python3
"""
テスト用マップを seed ごとに生成する（MatchSetting 形式 = 参考マップと同じ形）。
本選の条件（サイズ・エージェント数・スポット数・系列数）と募集要項・Q&A の範囲を満たす。

  python tools/gen.py 0-99                 # maps/gen/0000.json 〜 0099.json
  python tools/gen.py 0-9 --size 32        # サイズ固定
  python tools/gen.py 5 --out maps/gen     # seed 5 だけ

サイズは指定しなければ seed % 3 で 16 / 24 / 32 を順に回す。
"""
import argparse
import json
import os
import random
import sys
from collections import deque

PRESET = {16: (4, (10, 14)), 24: (5, (14, 20)), 32: (7, (20, 28))}  # size: (agents, brands)
DX_EVEN = [0, 1, 1, 1, 0, -1]
DX_ODD = [-1, 0, 1, 0, -1, -1]
DY = [-1, -1, 0, 1, 1, 0]
PLAIN, ROAD, MOUNTAIN, POND = 0, 1, 2, 3


def neighbors(W, H, x, y):
    for d in range(6):
        nx = x + (DX_EVEN[d] if y % 2 == 0 else DX_ODD[d])
        ny = y + DY[d]
        if 0 <= nx < W and 0 <= ny < H:
            yield nx, ny


def blobs(rng, cells, W, H, kind, ratio):
    """kind のかたまりを、全体の ratio になるまで育てる"""
    target = int(W * H * ratio)
    count = 0
    while count < target:
        x, y = rng.randrange(W), rng.randrange(H)
        size = rng.randint(3, max(4, W * H // 25))
        frontier = [(x, y)]
        while frontier and size > 0 and count < target:
            cx, cy = frontier.pop(rng.randrange(len(frontier)))
            if cells[cy][cx] != PLAIN:
                continue
            cells[cy][cx] = kind
            count += 1
            size -= 1
            frontier += [p for p in neighbors(W, H, cx, cy) if cells[p[1]][p[0]] == PLAIN]


def roads(rng, cells, W, H, ratio):
    """ランダムに曲がる線を何本も引いて道路網にする"""
    target = int(W * H * ratio)
    count = sum(r.count(ROAD) for r in cells)
    while count < target:
        x, y = rng.randrange(W), rng.randrange(H)
        d = rng.randrange(6)
        for _ in range(rng.randint(W // 2, W * 2)):
            if cells[y][x] != ROAD:
                cells[y][x] = ROAD
                count += 1
            if rng.random() < 0.2:
                d = (d + rng.choice([-1, 1])) % 6
            nx = x + (DX_EVEN[d] if y % 2 == 0 else DX_ODD[d])
            ny = y + DY[d]
            if not (0 <= nx < W and 0 <= ny < H):
                break
            x, y = nx, ny


def keep_largest_component(cells, W, H):
    """通れるセルを 1 つにつなげる（最大の連結成分以外は池にする）"""
    seen = [[-1] * W for _ in range(H)]
    comps = []
    for sy in range(H):
        for sx in range(W):
            if cells[sy][sx] == POND or seen[sy][sx] >= 0:
                continue
            q = deque([(sx, sy)])
            seen[sy][sx] = len(comps)
            comp = []
            while q:
                x, y = q.popleft()
                comp.append((x, y))
                for nx, ny in neighbors(W, H, x, y):
                    if cells[ny][nx] != POND and seen[ny][nx] < 0:
                        seen[ny][nx] = len(comps)
                        q.append((nx, ny))
            comps.append(comp)
    best = max(range(len(comps)), key=lambda i: len(comps[i]))
    for i, comp in enumerate(comps):
        if i != best:
            for x, y in comp:
                cells[y][x] = POND


def generate(seed, size=None):
    rng = random.Random(seed)
    n = size or [16, 24, 32][seed % 3]
    W = H = n
    na, (bmin, bmax) = PRESET[n]
    while True:
        cells = [[PLAIN] * W for _ in range(H)]
        blobs(rng, cells, W, H, POND, rng.uniform(0.12, 0.24))
        blobs(rng, cells, W, H, MOUNTAIN, rng.uniform(0.15, 0.30))
        roads(rng, cells, W, H, rng.uniform(0.20, 0.30))
        keep_largest_component(cells, W, H)
        plains = [y * W + x for y in range(H) for x in range(W) if cells[y][x] == PLAIN]
        kinds = {c for r in cells for c in r}
        if len(kinds) == 4 and len(plains) >= n + na + 5:
            break

    # スポット（平地・1 セル 1 つ）と系列（全系列が最低 1 つ）
    S = n
    nb = rng.randint(bmin, bmax)
    rng.shuffle(plains)
    spot_cells = plains[:S]
    brands = list(range(nb)) + [rng.randrange(nb) for _ in range(S - nb)]
    rng.shuffle(brands)
    spots = [{"brand": b, "pos": p, "stocks": rng.randint(1, na)} for b, p in zip(brands, spot_cells)]
    agents = plains[S:S + na]  # スポットのない平地・全員別のセル

    # 日程: 1 日のステップは W+H 〜 4(W+H)。seed ごとに「短め〜長め」を変える
    D = rng.randint(4, 10)
    base = W + H
    hi = rng.choice([1.5, 2.5, 4.0])
    day_steps = sorted(rng.randint(base, int(base * hi)) for _ in range(D))
    fuel = rng.randint(day_steps[0], day_steps[0] * 3)
    busy = rng.randint(1, 5)
    jammed = rng.randint(max(2, busy + 1), 10)
    return {
        "startsAt": 0,
        "daySeconds": [60] * D,
        "daySteps": day_steps,
        "map": {"height": H, "width": W, "cells": cells},
        "spots": spots,
        "agents": agents,
        "fuelLimits": fuel,
        "players": 10,
        "busyThreshold": busy,
        "jammedThreshold": jammed,
    }


def parse_seeds(s):
    out = []
    for part in s.split(","):
        if "-" in part:
            a, b = part.split("-")
            out += range(int(a), int(b) + 1)
        else:
            out.append(int(part))
    return out


def main():
    if not sys.stdout.isatty():
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser()
    ap.add_argument("seeds", help="例: 0-99 / 3 / 0,5,7")
    ap.add_argument("--size", type=int, choices=[16, 24, 32], default=None)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "maps", "gen"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    seeds = parse_seeds(args.seeds)
    for seed in seeds:
        with open(os.path.join(args.out, f"{seed:04d}.json"), "w", encoding="utf-8") as f:
            json.dump(generate(seed, args.size), f)
    print(f"{len(seeds)} マップを生成: {args.out}")


if __name__ == "__main__":
    main()
