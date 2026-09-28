// =====================================================================
//  solver: 固定拠点方式（hub）
//    補給車はその日の拠点（平地・山地のセル）へ移動して 1 日待つ
//    巡回車はルートに「拠点」を入れると満タンになる（補給車の到着前に着いたら待つ）
//    1 日の計画 = 巡回車ごとのノード列（スポット / 拠点）＋ 拠点の位置。貪欲挿入 → 焼きなまし
// =====================================================================
#include "common.hpp"

// ------------------------------ パラメータ ------------------------------
const double W_NEW      = 1000.0; // この試合で初めて取る系列（勝敗基準1）
const double W_DAY      = 100.0;  // 当日の種類数（勝敗基準2）
const double W_BALL     = 10.0;   // 玉数（勝敗基準3）
const double W_END_SPOT = 3.0;    // 日の終わりにスポット上にいる（翌日 1 ステップ目で獲得）
const double W_FUEL     = 0.05;   // 残り燃料 1 あたりの価値
const double W_TIME     = 0.01;   // 到着時刻の合計（早く着くほど良い）
const double W_BAD      = 1.0;    // 実行できないノード 1 つあたりのペナルティ
const double W_STRAND   = 300.0;  // 日の終わりに拠点へ戻れる燃料がない巡回車 1 台あたりのペナルティ

struct HubPlanner {
    const vector<Agent>& st;
    const vector<char>& collectedBefore;
    int steps;
    bool lastDay;
    Router router;
    vector<int> patrols, supplies;   // エージェント番号
    int P, K;                        // 巡回車数, 補給車(=拠点)数
    vector<int> hub, ready;          // [K] 拠点のセル, 補給車が拠点に着く時刻
    vector<int> hubCand;             // 拠点にできるセル（道路に置くと渋滞するので平地・山地のみ。救助のときは例外）
    vector<int> rescue;              // 救助が必要な巡回車のいるセル
    // ノード: 0..S-1 スポット, S..S+K-1 拠点 / 出発点: S+K+u
    vector<const PathTable*> tf, tc; // 各出発点からの 最速 / 燃料最小 の表
    vector<const PathTable*> supTbl; // [K] 補給車の現在地からの最速の表

    HubPlanner(const vector<Agent>& st_, const vector<int>& status, const vector<char>& before, int steps_, bool last)
        : st(st_), collectedBefore(before), steps(steps_), lastDay(last), router(status) {
        for (int i = 0; i < NA; i++) (st[i].kind == 0 ? patrols : supplies).push_back(i);
        P = patrols.size(); K = supplies.size();
        tf.resize(S + K + P); tc.resize(S + K + P);
        for (int s = 0; s < S; s++) { tf[s] = &router.get(spots[s].pos, 0); tc[s] = &router.get(spots[s].pos, 1); }
        for (int u = 0; u < P; u++) {
            int c = st[patrols[u]].pos;
            tf[S + K + u] = &router.get(c, 0); tc[S + K + u] = &router.get(c, 1);
        }
        supTbl.resize(K);
        for (int j = 0; j < K; j++) supTbl[j] = &router.get(st[supplies[j]].pos, 0);
        for (int p = 0; p < NC; p++) if (passable(p) && cellType[p] != ROAD) hubCand.push_back(p);
        // 補給車の今の位置まで行けない（= 自力では補給を受けられない）巡回車は救助の対象
        for (int u = 0; u < P; u++) {
            int c = st[patrols[u]].pos;
            for (int j = 0; j < K; j++)
                if (tc[S + K + u]->f[st[supplies[j]].pos] > st[patrols[u]].fuel) { rescue.push_back(c); break; }
        }
        for (int c : rescue) if (cellType[c] == ROAD) hubCand.push_back(c);
        hub.assign(K, -1); ready.assign(K, INT_MAX);
        initHubs();
    }

    int cellOf(int node) const { return node < S ? spots[node].pos : hub[node - S]; }

    void setHub(int j, int c) {
        hub[j] = c;
        ready[j] = supTbl[j]->t[c];
        tf[S + j] = &router.get(c, 0); tc[S + j] = &router.get(c, 1);
    }

    // 初期の拠点: 候補（スポットのセル + 補給車の今の位置）から、スポットへの平均距離 + 補給車の移動時間 が小さい所
    void initHubs() {
        vector<int> cand;
        for (int s = 0; s < S; s++) cand.push_back(spots[s].pos);
        for (int j = 0; j < K; j++) if (cellType[st[supplies[j]].pos] != ROAD) cand.push_back(st[supplies[j]].pos);
        vector<int> nearest(S, INT_MAX);  // 決定済みの拠点からの最短
        vector<char> fixedHub(K, 0);
        for (int c : rescue) {
            int bj = -1;
            for (int j = 0; j < K; j++)
                if (!fixedHub[j] && supTbl[j]->t[c] != INT_MAX && (bj < 0 || supTbl[j]->t[c] < supTbl[bj]->t[c])) bj = j;
            if (bj < 0) break;
            fixedHub[bj] = 1;
            setHub(bj, c);
            const PathTable& pt = router.get(c, 0);
            for (int s = 0; s < S; s++) nearest[s] = min(nearest[s], pt.t[spots[s].pos]);
        }
        for (int j = 0; j < K; j++) {
            if (fixedHub[j]) continue;
            double bestCost = 1e18; int bestC = st[supplies[j]].pos;
            for (int c : cand) {
                int travel = supTbl[j]->t[c];
                if (travel == INT_MAX) continue;
                const PathTable& pt = router.get(c, 0);
                double sum = 0;
                for (int s = 0; s < S; s++) sum += min(nearest[s], pt.t[spots[s].pos]);
                double cost = sum / S + travel;
                if (cost < bestCost) { bestCost = cost; bestC = c; }
            }
            setHub(j, bestC);
            const PathTable& pt = router.get(bestC, 0);
            for (int s = 0; s < S; s++) nearest[s] = min(nearest[s], pt.t[spots[s].pos]);
        }
    }

    // ノードを 1 つ進める。wait = 拠点で補給車を待ったステップ数
    inline bool advance(int src, int node, int& t, int& f, int& mode, int& wait) const {
        int c = cellOf(node);
        const PathTable* A = tf[src];
        const PathTable* C = tc[src];
        if (A->t[c] == INT_MAX) return false;
        if (node >= S && ready[node - S] > steps) return false;
        if (f >= A->f[c] && t + A->t[c] <= steps) { t += A->t[c]; f -= A->f[c]; mode = 0; }
        else if (f >= C->f[c] && t + C->t[c] <= steps) { t += C->t[c]; f -= C->f[c]; mode = 1; }
        else return false;
        wait = 0;
        if (node >= S) {
            int r = ready[node - S];
            if (t < r) { wait = r - t; t = r; }
            f = FUEL_LIMIT;
        }
        return true;
    }

    // 評価（大きいほど良い）
    vector<int> visits, hit, seen;
    int stamp = 0;
    double eval(const vector<vector<int>>& routes) {
        visits.assign(S, 0); hit.assign(B, 0);
        if ((int)seen.size() != S) seen.assign(S, 0);
        double sumT = 0; int bad = 0, endSpot = 0, strand = 0; long long fuelLeft = 0;
        auto mark = [&](int c) {
            int s = spotAt[c];
            if (s >= 0 && seen[s] != stamp) { seen[s] = stamp; visits[s]++; hit[spots[s].brand] = 1; }
            return s >= 0;
        };
        for (int u = 0; u < P; u++) {
            ++stamp;
            int t = 0, f = st[patrols[u]].fuel, src = S + K + u, mode, wait;
            bool onSpot = mark(st[patrols[u]].pos);
            for (int node : routes[u]) {
                if (!advance(src, node, t, f, mode, wait)) { bad++; continue; }
                src = node;
                onSpot = mark(cellOf(node));
                sumT += t;
            }
            if (onSpot) endSpot++;
            fuelLeft += f;
            // 最終日以外: どこかの拠点まで燃料最小の経路で戻れないなら減点（翌日以降に補給できなくなる）
            if (!lastDay && K > 0) {
                int need = INT_MAX;
                for (int j = 0; j < K; j++) need = min(need, tc[src]->f[hub[j]]);
                if (f < need) strand++;
            }
        }
        int balls = 0, dayB = 0, newB = 0;
        for (int s = 0; s < S; s++) balls += min(visits[s], spots[s].stock);
        for (int b = 0; b < B; b++) if (hit[b]) { dayB++; if (!collectedBefore[b]) newB++; }
        double sc = W_NEW * newB + W_DAY * dayB + W_BALL * balls - W_TIME * sumT - W_BAD * bad - W_STRAND * strand;
        if (!lastDay) sc += W_END_SPOT * endSpot + W_FUEL * fuelLeft;
        return sc;
    }

    // 貪欲挿入（スポットと拠点の両方を候補に）
    vector<vector<int>> greedy() {
        vector<vector<int>> routes(P);
        double cur = eval(routes);
        while (true) {
            double best = cur; int bu = -1, bp = -1, bs = -1;
            for (int u = 0; u < P; u++)
                for (int s = 0; s < S + K; s++)
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

    int randomNode() { return rng.nextInt(S + K); }

    // 焼きなまし（ルート + 拠点の位置）
    vector<vector<int>> anneal(vector<vector<int>> routes, double timeMs) {
        if (P == 0 || timeMs <= 0) return routes;
        Timer tm;
        double cur = eval(routes), best = cur;
        auto bestR = routes;
        auto bestHub = hub;
        const double T0 = W_BALL * 3, T1 = W_TIME;
        long long iter = 0;
        double T = T0;
        while (true) {
            if ((iter & 255) == 0) {
                double el = tm.ms();
                if (el > timeMs) break;
                T = T0 * pow(T1 / T0, el / timeMs);
            }
            iter++;
            int type = rng.nextInt(K > 0 ? 7 : 6);
            if (type == 6) {                       // 拠点を動かす
                int j = rng.nextInt(K), old = hub[j], c = old;
                if (rng.nextInt(2)) c = hubCand[rng.nextInt(hubCand.size())];
                else {
                    for (int k = 1 + rng.nextInt(3); k > 0; k--) {
                        int nx = neighbor(c, rng.nextInt(6));
                        if (passable(nx) && cellType[nx] != ROAD) c = nx;
                    }
                }
                if (c == old) continue;
                setHub(j, c);
                double v = eval(routes);
                if (v >= cur || rng.nextDouble() < exp((v - cur) / T)) {
                    cur = v;
                    if (cur > best) { best = cur; bestR = routes; bestHub = hub; }
                } else setHub(j, old);
                continue;
            }
            auto nr = routes;
            int u = rng.nextInt(P);
            auto& r = nr[u];
            if (type == 0) {                       // 挿入
                r.insert(r.begin() + rng.nextInt(r.size() + 1), randomNode());
            } else if (type == 1) {                // 削除
                if (r.empty()) continue;
                r.erase(r.begin() + rng.nextInt(r.size()));
            } else if (type == 2) {                // 別の巡回車へ移動
                if (r.empty() || P < 2) continue;
                int i = rng.nextInt(r.size()), s = r[i], v = rng.nextInt(P);
                r.erase(r.begin() + i);
                nr[v].insert(nr[v].begin() + rng.nextInt(nr[v].size() + 1), s);
            } else if (type == 3) {                // 2 点交換
                int v = rng.nextInt(P);
                if (r.empty() || nr[v].empty()) continue;
                swap(r[rng.nextInt(r.size())], nr[v][rng.nextInt(nr[v].size())]);
            } else if (type == 4) {                // 差し替え（スポットなら半分は同じ系列）
                if (r.empty()) continue;
                int i = rng.nextInt(r.size());
                if (r[i] < S && rng.nextInt(2)) {
                    int b = spots[r[i]].brand, cnt = 0, pick = -1;
                    for (int s = 0; s < S; s++) if (spots[s].brand == b && rng.nextInt(++cnt) == 0) pick = s;
                    r[i] = pick;
                } else r[i] = randomNode();
            } else {                               // 区間反転
                if (r.size() < 2) continue;
                int i = rng.nextInt(r.size()), j = rng.nextInt(r.size());
                if (i > j) swap(i, j);
                reverse(r.begin() + i, r.begin() + j + 1);
            }
            double v = eval(nr);
            if (v >= cur || rng.nextDouble() < exp((v - cur) / T)) {
                routes = move(nr); cur = v;
                if (cur > best) { best = cur; bestR = routes; bestHub = hub; }
            }
        }
        for (int j = 0; j < K; j++) setHub(j, bestHub[j]);
        cerr << "[anneal] iter=" << iter << " score=" << best << " hubs=";
        for (int j = 0; j < K; j++) cerr << hub[j] << "(ready " << ready[j] << ") ";
        cerr << "\n";
        return bestR;
    }

    // ノード列 → 行動計画
    vector<vector<int>> toPlan(const vector<vector<int>>& routes) {
        vector<vector<int>> plan(NA);
        for (int u = 0; u < P; u++) {
            vector<int>& pa = plan[patrols[u]];
            int t = 0, f = st[patrols[u]].fuel, src = S + K + u, cur = st[patrols[u]].pos, mode, wait;
            for (int node : routes[u]) {
                if (!advance(src, node, t, f, mode, wait)) continue;
                appendMoves(router.path(cur, cellOf(node), mode), pa);
                if (wait > 0) pa.push_back(-wait);
                src = node; cur = cellOf(node);
            }
            if (t < steps) pa.push_back(-(steps - t));
        }
        for (int j = 0; j < K; j++) {
            vector<int>& sa = plan[supplies[j]];
            int used;
            sa = movesWithin(router.path(st[supplies[j]].pos, hub[j], 0), router.status, steps, used);
            if (used < steps) sa.push_back(-(steps - used));
        }
        return plan;
    }
};

vector<vector<int>> planDay(const vector<Agent>& st, const vector<int>& status, const vector<char>& before,
                            int steps, bool lastDay, double timeMs) {
    HubPlanner hp(st, status, before, steps, lastDay);
    Timer tm;  // 貪欲挿入の時間も持ち時間に含める
    auto routes = hp.greedy();
    routes = hp.anneal(routes, timeMs - tm.ms());
    auto plan = hp.toPlan(routes);
    DayResult r = simulateDay(st, plan, status, steps);
    if (!r.valid) {
        cerr << "[planDay] 計画が不正: " << r.error << " → 全員待機\n";
        return allWait(steps);
    }
    return plan;
}

// 種別の候補: 補給車 0 台 / 1 台（全員を 1 回ずつ試す）/ 2 台以上（スポットへの距離の合計が小さい順）
vector<vector<int>> kindCandidates() {
    vector<int> status0(NC, 0);
    Router router(status0);
    vector<pair<long long, int>> cost;
    for (int i = 0; i < NA; i++) {
        const PathTable& pt = router.get(agentStart[i], 0);
        long long c = 0;
        for (int s = 0; s < S; s++) c += pt.t[spots[s].pos];
        cost.push_back({c, i});
    }
    sort(cost.begin(), cost.end());
    vector<vector<int>> cands;
    cands.push_back(vector<int>(NA, 0));
    for (int i = 0; i < NA; i++) {
        vector<int> k(NA, 0);
        k[i] = 1;
        cands.push_back(k);
    }
    for (int m = 2; m <= NA / 2; m++) {
        vector<int> k(NA, 0);
        for (int x = 0; x < m; x++) k[cost[x].second] = 1;
        cands.push_back(k);
    }
    return cands;
}

int main() { return solverMain(); }
