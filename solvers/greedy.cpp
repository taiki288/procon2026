// =====================================================================
//  solver: 素朴な貪欲（greedy）… 対戦相手の模型
//    補給車 1 台はマップ中央付近の拠点で待つ
//    巡回車は「今日まだチームが取っていない系列の一番近いスポット」へ順に向かう
//    （全系列を取ったら、自分が今日まだ行っていない在庫のあるスポットへ）
//    行き先に行ったあと拠点へ戻れない燃料なら、先に拠点へ補給しに行く
//    経路は常に最速。探索はしない
// =====================================================================
#include "common.hpp"

// 拠点: スポットのセルのうち、全スポットへの距離の合計が一番小さい所（道路以外）
int centralCell(Router& router) {
    long long best = LLONG_MAX; int bc = spots[0].pos;
    for (int s = 0; s < S; s++) {
        int c = spots[s].pos;
        const PathTable& pt = router.get(c, 0);
        long long sum = 0;
        for (int k = 0; k < S; k++) sum += pt.t[spots[k].pos] == INT_MAX ? 1000000 : pt.t[spots[k].pos];
        if (sum < best) { best = sum; bc = c; }
    }
    return bc;
}

vector<vector<int>> planDay(const vector<Agent>& st, const vector<int>& status, const vector<char>& /*before*/,
                            int steps, bool /*lastDay*/, double /*timeMs*/) {
    Router router(status);
    vector<vector<int>> plan(NA);
    int hub = centralCell(router), ready = INT_MAX;

    // 補給車: 1 台目は拠点へ、残りはその場で待機
    bool first = true;
    for (int i = 0; i < NA; i++) {
        if (st[i].kind != 1) continue;
        int used = 0;
        if (first) {
            plan[i] = movesWithin(router.path(st[i].pos, hub, 0), router.status, steps, used);
            if (st[i].pos == hub || router.get(st[i].pos, 0).t[hub] <= steps) ready = router.get(st[i].pos, 0).t[hub];
            first = false;
        }
        if (used < steps) plan[i].push_back(-(steps - used));
    }
    bool hasHub = ready <= steps;
    const PathTable* toHubFrom = nullptr;  // 各セル → 拠点 の距離は「拠点から」の表で代用（向きの差は小さいので近似）
    if (hasHub) toHubFrom = &router.get(hub, 0);

    // 巡回車: 時刻の早い順に 1 台ずつ次の行き先を決める
    struct P { int id, cell, t, fuel; bool done; vector<char> visited; };
    vector<P> ps;
    for (int i = 0; i < NA; i++) if (st[i].kind == 0) ps.push_back({i, st[i].pos, 0, st[i].fuel, false, vector<char>(S, 0)});
    vector<char> brandTaken(B, 0);
    vector<int> stock(S);
    for (int s = 0; s < S; s++) stock[s] = spots[s].stock;
    auto take = [&](P& p, int cell) {
        int s = spotAt[cell];
        if (s >= 0 && !p.visited[s] && stock[s] > 0) { p.visited[s] = 1; stock[s]--; brandTaken[spots[s].brand] = 1; }
    };
    for (auto& p : ps) take(p, p.cell);  // 開始地点のスポット（1 ステップ目で取れる）

    auto moveTo = [&](P& p, int dst) {
        vector<int> cells = router.path(p.cell, dst, 0);
        appendMoves(cells, plan[p.id]);
        const PathTable& pt = router.get(p.cell, 0);
        p.t += pt.t[dst]; p.fuel -= pt.f[dst]; p.cell = dst;
    };
    auto refuelIfHub = [&](P& p) {
        if (!hasHub || p.cell != hub) return;
        if (p.t < ready) { plan[p.id].push_back(-(ready - p.t)); p.t = ready; }
        p.fuel = FUEL_LIMIT;
    };

    while (true) {
        int k = -1;
        for (size_t i = 0; i < ps.size(); i++) if (!ps[i].done && (k < 0 || ps[i].t < ps[k].t)) k = i;
        if (k < 0) break;
        P& p = ps[k];
        const PathTable& pt = router.get(p.cell, 0);
        // 行き先の候補: まず未取得の系列、なければ自分が未訪問で在庫のあるスポット
        int best = -1, bestT = INT_MAX; bool needFuel = false;
        for (int pass = 0; pass < 2 && best < 0; pass++) {
            for (int s = 0; s < S; s++) {
                int c = spots[s].pos;
                if (p.visited[s] || stock[s] <= 0) continue;
                if (pass == 0 && brandTaken[spots[s].brand]) continue;
                int tt = pt.t[c];
                if (tt == INT_MAX || p.t + tt > steps) continue;
                int back = hasHub ? toHubFrom->f[c] + 2 : 0;  // 往復の向きの差ぶん余裕を持たせる
                if (p.fuel < pt.f[c] + back) { needFuel = true; continue; }
                if (tt < bestT) { bestT = tt; best = s; }
            }
        }
        if (best >= 0) {
            moveTo(p, spots[best].pos);
            take(p, p.cell);
            refuelIfHub(p);
            continue;
        }
        // 燃料が足りずに行けない所があるなら、拠点へ補給しに行く
        if (needFuel && hasHub && p.cell != hub && pt.t[hub] != INT_MAX && p.t + pt.t[hub] <= steps && p.fuel >= pt.f[hub]) {
            moveTo(p, hub);
            take(p, p.cell);
            refuelIfHub(p);
            continue;
        }
        p.done = true;
    }
    for (auto& p : ps) if (p.t < steps) plan[p.id].push_back(-(steps - p.t));

    DayResult r = simulateDay(st, plan, status, steps);
    if (!r.valid) {
        cerr << "[greedy] 計画が不正: " << r.error << " → 全員待機\n";
        return allWait(steps);
    }
    return plan;
}

// 種別: 拠点に一番近いエージェントを補給車に（1 台だけ）
vector<vector<int>> kindCandidates() {
    vector<int> status0(NC, 0);
    Router router(status0);
    int hub = centralCell(router);
    const PathTable& pt = router.get(hub, 0);
    int sup = 0;
    for (int i = 1; i < NA; i++) if (pt.t[agentStart[i]] < pt.t[agentStart[sup]]) sup = i;
    vector<int> kinds(NA, 0);
    kinds[sup] = 1;
    return {kinds};
}

int main() { return solverMain(); }
