"""JSON（サーバー/ログ形式）と solver 用テキスト入力の相互変換。"""


def setting_to_problem(setting):
    """GET /setting の MatchSetting → 試合設定(config)の problem 形式"""
    return {
        "width": setting["map"]["width"],
        "height": setting["map"]["height"],
        "cells": setting["map"]["cells"],
        "spots": setting["spots"],
        "agentStarts": setting["agents"],
        "fuelLimits": setting["fuelLimits"],
        "daySteps": setting["daySteps"],
        "daySeconds": setting["daySeconds"],
        "busyThreshold": setting["busyThreshold"],
        "jammedThreshold": setting["jammedThreshold"],
    }


def load_problem(obj):
    """MatchSetting / config({problem, teams}) / ログ({problem, teams}) のどれからでも problem を取り出す"""
    if "problem" in obj:
        return obj["problem"]
    if "map" in obj:
        return setting_to_problem(obj)
    raise ValueError("problem が見つかりません")


def _statuses(traffics):
    lines = [str(len(traffics))]
    lines += [f'{t["pos"]} {t["status"]}' for t in traffics]
    return lines


def build_input(problem, players, mode, time_ms, day=None, state=None, history=None):
    """
    solver の標準入力を作る。
      mode    : "KIND" / "DAY"
      state   : GET / の MatchState（DAY のみ）
      history : [{"traffics": [...], "plan": [[...], ...]}, ...]  過去の日（DAY のみ）
    """
    L = [f'{problem["height"]} {problem["width"]}']
    L += [" ".join(map(str, row)) for row in problem["cells"]]
    L.append(str(len(problem["spots"])))
    L += [f'{s["brand"]} {s["pos"]} {s["stocks"]}' for s in problem["spots"]]
    L.append(str(len(problem["agentStarts"])))
    L.append(" ".join(map(str, problem["agentStarts"])))
    L.append(str(problem["fuelLimits"]))
    L.append(str(len(problem["daySteps"])))
    L.append(" ".join(map(str, problem["daySteps"])))
    L.append(" ".join(map(str, problem["daySeconds"])))
    L.append(f'{players} {problem["busyThreshold"]} {problem["jammedThreshold"]}')
    L.append(mode)
    L.append(str(int(time_ms)))
    if mode == "DAY":
        L.append(str(day))
        L += [f'{a["kind"]} {a["pos"]} {a["fuel"]}' for a in state["agents"]]
        L += _statuses(state["traffics"])
        L.append(str(len(state["others"])))
        for o in state["others"]:
            L.append(str(o["id"]))
            L += [f'{a["kind"]} {a["pos"]} {a["fuel"]}' for a in o["agents"]]
        for h in history:
            L += _statuses(h["traffics"])
            L += [" ".join(map(str, [len(p)] + p)) for p in h["plan"]]
    return "\n".join(L) + "\n"


def parse_kinds(text, n):
    for line in text.splitlines():
        v = line.split()
        if v:
            kinds = [int(x) for x in v]
            if len(kinds) == n and all(k in (0, 1) for k in kinds):
                return kinds
            break
    return None


def parse_plan(text, n):
    rows = [[int(x) for x in line.split()] for line in text.splitlines() if line.strip()]
    return rows if len(rows) == n else None
