// =====================================================================
//  solver: 随伴方式（escort）
//    補給車は巡回車 1 台に合流して、同じ行動をとる → その巡回車は燃料無限
//    1 日の計画 = 各巡回車が立ち寄るスポットの列。貪欲挿入 → 焼きなまし
// =====================================================================
#include "common.hpp"

// ------------------------------ パラメータ ------------------------------
const double W_NEW      = 1000.0; // この試合で初めて取る系列（勝敗基準1）
const double W_DAY      = 100.0;  // 当日の種類数（勝敗基準2）
const double W_BALL     = 10.0;   // 玉数（勝敗基準3）
const double W_END_SPOT = 3.0;    // 日の終わりにスポット上にいる（翌日 1 ステップ目で獲得）
const double W_FUEL     = 0.05;   // 残り燃料 1 あたりの価値（補給なしの巡回車）
const double W_TIME     = 0.01;   // 到着時刻の合計（早く着くほど良い）
const double W_BAD      = 1.0;    // 実行できない経由地 1 つあたりのペナルティ

// ユニット = 巡回車 1 台（＋随伴する補給車）
struct Unit { int patrol, supply, start, t0, fuel; };

struct DayPlanner {
    const vector<Agent>& st;
    const vector<char>& collectedBefore;  // この試合ですでに取った系列
    int steps;
    bool lastDay;
    Router router;
    vector<Unit> units;
    vector<vector<int>> supplyPrefix;     // 補給車が合流するまでの行動
    vector<int> idleSupply;               // どのユニットにも属さない補給車
    // leg[src][s] : src(0..S-1 はスポット, S+u はユニット開始点) → スポット s
    struct Leg { int tf, ff, tc, fc; };
    vector<vector<Leg>> leg;

    DayPlanner(const vector<Agent>& st_, const vector<int>& status, const vector<char>& before, int steps_, bool last)
        : st(st_), collectedBefore(before), steps(steps_), lastDay(last), router(status) {
        buildUnits();
        int nsrc = S + units.size();
        leg.assign(nsrc, vector<Leg>(S));
        for (int src = 0; src < nsrc; src++) {
            int c = src < S ? spots[src].pos : units[src - S].start;
            const PathTable& f = router.get(c, 0);
            const PathTable& ch = router.get(c, 1);
            for (int s = 0; s < S; s++) {
                int p = spots[s].pos;
                leg[src][s] = {f.t[p], f.f[p], ch.t[p], ch.f[p]};
            }
        }
    }

    void buildUnits() {
        vector<int> patrols, supplies;
        for (int i = 0; i < NA; i++) (st[i].kind == 0 ? patrols : supplies).push_back(i);
        vector<int> pairOf(NA, -1);
        // 同じセルにいる巡回車と組む → 残りは一番近い巡回車と組む
        for (int j : supplies)
            for (int p : patrols)
                if (pairOf[p] < 0 && st[p].pos == st[j].pos) { pairOf[p] = j; pairOf[j] = p; break; }
        for (int j : supplies) {
            if (pairOf[j] >= 0) continue;
            const PathTable& pt = router.get(st[j].pos, 0);
            int best = -1;
            for (int p : patrols) if (pairOf[p] < 0 && (best < 0 || pt.t[st[p].pos] < pt.t[st[best].pos])) best = p;
            if (best >= 0) { pairOf[best] = j; pairOf[j] = best; }
        }
        supplyPrefix.assign(NA, {});
        for (int p : patrols) {
            int j = pairOf[p];
            if (j < 0) { units.push_back({p, -1, st[p].pos, 0, st[p].fuel}); continue; }
            vector<int> cells = router.path(st[j].pos, st[p].pos, 0);
            int m = pathSteps(cells, router.status);
            if (m < steps) {
                appendMoves(cells, supplyPrefix[j]);
                units.push_back({p, j, st[p].pos, m, INF_FUEL});
            } else {
                // 今日中に合流できない: 補給車は行けるところまで近づく
                units.push_back({p, -1, st[p].pos, 0, st[p].fuel});
                int used;
                supplyPrefix[j] = movesWithin(cells, router.status, steps, used);
                idleSupply.push_back(j);
            }
        }
        for (int j : supplies) if (pairOf[j] < 0) idleSupply.push_back(j);
    }

    // 経由地を 1 つ進める。戻り値: 実行できたか（mode に使った経路を返す）
    inline bool advance(int src, int s, int& t, int& f, int& mode) const {
        const Leg& L = leg[src][s];
        if (L.tf == INT_MAX) return false;
        if (f >= L.ff && t + L.tf <= steps) { t += L.tf; if (f < INF_FUEL) f -= L.ff; mode = 0; return true; }
        if (f >= L.fc && t + L.tc <= steps) { t += L.tc; if (f < INF_FUEL) f -= L.fc; mode = 1; return true; }
        return false;
    }

    // 評価（大きいほど良い）
    vector<int> visits, hit, seen;
    int stamp = 0;
    double eval(const vector<vector<int>>& routes) {
        visits.assign(S, 0); hit.assign(B, 0);
        if ((int)seen.size() != S) seen.assign(S, 0);
        double sumT = 0; int bad = 0, endSpot = 0; long long fuelLeft = 0;
        for (size_t u = 0; u < units.size(); u++) {
            const Unit& U = units[u];
            ++stamp;
            int t = U.t0, f = U.fuel, src = S + u, mode;
            int s0 = spotAt[U.start];
            if (s0 >= 0) { seen[s0] = stamp; visits[s0]++; hit[spots[s0].brand] = 1; }
            bool onSpot = s0 >= 0;
            for (int s : routes[u]) {
                if (!advance(src, s, t, f, mode)) { bad++; continue; }
                src = s; onSpot = true;
                sumT += t;
                if (seen[s] != stamp) { seen[s] = stamp; visits[s]++; hit[spots[s].brand] = 1; }
            }
            if (onSpot) endSpot++;
            if (U.fuel < INF_FUEL) fuelLeft += f;
        }
        int balls = 0, dayB = 0, newB = 0;
        for (int s = 0; s < S; s++) balls += min(visits[s], spots[s].stock);
        for (int b = 0; b < B; b++) if (hit[b]) { dayB++; if (!collectedBefore[b]) newB++; }
        double sc = W_NEW * newB + W_DAY * dayB + W_BALL * balls - W_TIME * sumT - W_BAD * bad;
        if (!lastDay) sc += W_END_SPOT * endSpot + W_FUEL * fuelLeft;
        return sc;
    }

    // 貪欲挿入
    vector<vector<int>> greedy() {
        vector<vector<int>> routes(units.size());
        double cur = eval(routes);
        while (true) {
            double best = cur; int bu = -1, bp = -1, bs = -1;
            for (size_t u = 0; u < units.size(); u++)
                for (int s = 0; s < S; s++)
                    for (size_t p = 0; p <= routes[u].size(); p++) {
                        routes[u].insert(routes[u].begin() + p, s);
                        double v = eval(routes);
                        routes[u].erase(routes[u].begin() + p);
                        if (v > best + 1e-9) { best = v; bu = u; bp = p; bs = s; }
                    }
            if (bu < 0) break;
            routes[bu].insert(routes[bu].begin() + bp, bs);
            cur = best;
        }
        return routes;
    }

    // 焼きなまし
    vector<vector<int>> anneal(vector<vector<int>> routes, double timeMs) {
        if (units.empty() || timeMs <= 0) return routes;
        Timer tm;
        double cur = eval(routes), best = cur;
        auto bestR = routes;
        const double T0 = W_BALL * 3, T1 = W_TIME;
        int U = units.size();
        long long iter = 0;
        double T = T0;
        while (true) {
            if ((iter & 255) == 0) {
                double el = tm.ms();
                if (el > timeMs) break;
                T = T0 * pow(T1 / T0, el / timeMs);
            }
            iter++;
            auto nr = routes;
            int type = rng.nextInt(6), u = rng.nextInt(U);
            auto& r = nr[u];
            if (type == 0) {                       // 挿入
                r.insert(r.begin() + rng.nextInt(r.size() + 1), rng.nextInt(S));
            } else if (type == 1) {                // 削除
                if (r.empty()) continue;
                r.erase(r.begin() + rng.nextInt(r.size()));
            } else if (type == 2) {                // 別ユニットへ移動
                if (r.empty() || U < 2) continue;
                int i = rng.nextInt(r.size()), s = r[i], v = rng.nextInt(U);
                r.erase(r.begin() + i);
                nr[v].insert(nr[v].begin() + rng.nextInt(nr[v].size() + 1), s);
            } else if (type == 3) {                // 2 点交換（ユニット間も可）
                int v = rng.nextInt(U);
                if (r.empty() || nr[v].empty()) continue;
                swap(r[rng.nextInt(r.size())], nr[v][rng.nextInt(nr[v].size())]);
            } else if (type == 4) {                // 差し替え（半分は同じ系列の別スポット）
                if (r.empty()) continue;
                int i = rng.nextInt(r.size());
                if (rng.nextInt(2)) {
                    int b = spots[r[i]].brand, cnt = 0, pick = -1;
                    for (int s = 0; s < S; s++) if (spots[s].brand == b && rng.nextInt(++cnt) == 0) pick = s;
                    r[i] = pick;
                } else r[i] = rng.nextInt(S);
            } else {                               // 区間反転
                if (r.size() < 2) continue;
                int i = rng.nextInt(r.size()), j = rng.nextInt(r.size());
                if (i > j) swap(i, j);
                reverse(r.begin() + i, r.begin() + j + 1);
            }
            double v = eval(nr);
            if (v >= cur || rng.nextDouble() < exp((v - cur) / T)) {
                routes = move(nr); cur = v;
                if (cur > best) { best = cur; bestR = routes; }
            }
        }
        cerr << "[anneal] iter=" << iter << " score=" << best << "\n";
        return bestR;
    }

    // 経由地の列 → 行動計画
    vector<vector<int>> toPlan(const vector<vector<int>>& routes) {
        vector<vector<int>> plan(NA);
        for (size_t u = 0; u < units.size(); u++) {
            const Unit& U = units[u];
            vector<int> moves;
            int t = U.t0, f = U.fuel, src = S + u, cur = U.start, mode;
            for (int s : routes[u]) {
                if (!advance(src, s, t, f, mode)) continue;
                appendMoves(router.path(cur, spots[s].pos, mode), moves);
                src = s; cur = spots[s].pos;
            }
            vector<int>& pa = plan[U.patrol];
            if (U.t0 > 0) pa.push_back(-U.t0);
            pa.insert(pa.end(), moves.begin(), moves.end());
            if (t < steps) pa.push_back(-(steps - t));
            if (U.supply >= 0) {
                vector<int>& sa = plan[U.supply];
                sa = supplyPrefix[U.supply];
                sa.insert(sa.end(), moves.begin(), moves.end());
                if (t < steps) sa.push_back(-(steps - t));
            }
        }
        for (int j : idleSupply) {
            plan[j] = supplyPrefix[j];
            int t = 0, cur = st[j].pos;
            for (int d : plan[j]) { t += stepCost(cur, router.status); cur = neighbor(cur, d); }
            if (t < steps) plan[j].push_back(-(steps - t));
        }
        return plan;
    }
};

vector<vector<int>> planDay(const vector<Agent>& st, const vector<int>& status, const vector<char>& before,
                            int steps, bool lastDay, double timeMs) {
    DayPlanner dp(st, status, before, steps, lastDay);
    Timer tm;  // 貪欲挿入の時間も持ち時間に含める
    auto routes = dp.greedy();
    routes = dp.anneal(routes, timeMs - tm.ms());
    auto plan = dp.toPlan(routes);
    DayResult r = simulateDay(st, plan, status, steps);
    if (!r.valid) {
        cerr << "[planDay] 計画が不正: " << r.error << " → 全員待機\n";
        return allWait(steps);
    }
    return plan;
}

// 補給車の候補: スタート位置が近いペアから順に「片方を補給車」にする（0〜NA/2 台）
vector<vector<int>> kindCandidates() {
    vector<int> status0(NC, 0);
    Router router(status0);
    vector<tuple<int, int, int>> pairs;
    for (int i = 0; i < NA; i++) {
        const PathTable& pt = router.get(agentStart[i], 0);
        for (int j = i + 1; j < NA; j++) pairs.push_back({pt.t[agentStart[j]], i, j});
    }
    sort(pairs.begin(), pairs.end());
    vector<vector<int>> cands;
    cands.push_back(vector<int>(NA, 0));
    vector<char> used(NA, 0);
    vector<int> kinds(NA, 0);
    for (auto [d, i, j] : pairs) {
        if ((int)cands.size() > NA / 2) break;
        if (used[i] || used[j]) continue;
        used[i] = used[j] = 1;
        kinds[j] = 1;
        cands.push_back(kinds);
    }
    return cands;
}

int main() { return solverMain(); }
