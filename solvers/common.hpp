// =====================================================================
//  solver 共通部分（問題・シミュレータ・経路・入力・KIND/DAY の流れ）
//
//  各 solver(.cpp) は次の 2 つを定義して、main で solverMain() を呼ぶだけ:
//    vector<vector<int>> planDay(st, status, collectedBefore, steps, lastDay, timeMs)
//    vector<vector<int>> kindCandidates()      // 試す種別の候補
// =====================================================================
#pragma once
#include <bits/stdc++.h>
using namespace std;

const int INF_FUEL = 1 << 29;

// ------------------------------ ユーティリティ ------------------------------
struct Timer {
    chrono::steady_clock::time_point st = chrono::steady_clock::now();
    double ms() const { return chrono::duration<double, milli>(chrono::steady_clock::now() - st).count(); }
};
struct Rng {
    uint64_t x = 88172645463325252ULL;
    uint64_t next() { x ^= x << 7; x ^= x >> 9; return x; }
    int nextInt(int n) { return (int)(next() % (uint64_t)n); }
    double nextDouble() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
} rng;

// ------------------------------ 問題 ------------------------------
enum Terrain { PLAIN = 0, ROAD = 1, MOUNTAIN = 2, POND = 3 };

int H, W, NC;                 // 縦, 横, セル数
vector<int> cellType;         // [NC]
struct Spot { int brand, pos, stock; };
vector<Spot> spots;           // brand は 0..B-1 に振り直し済み
int S, B;                     // スポット数, 系列数
vector<int> spotAt;           // セル -> スポット番号 (なければ -1)
int NA;                       // 1 チームのエージェント数
vector<int> agentStart;
int FUEL_LIMIT, D, PLAYERS, BUSY, JAM;
vector<int> daySteps, daySeconds;

// 方向: 0 左上, 1 右上, 2 右, 3 右下, 4 左下, 5 左（偶数行が右にずれる）
const int DX_EVEN[6] = {0, 1, 1, 1, 0, -1}, DX_ODD[6] = {-1, 0, 1, 0, -1, -1};
const int DY[6] = {-1, -1, 0, 1, 1, 0};
inline int neighbor(int p, int d) {
    int x = p % W, y = p / W;
    int nx = x + ((y % 2 == 0) ? DX_EVEN[d] : DX_ODD[d]), ny = y + DY[d];
    if (nx < 0 || nx >= W || ny < 0 || ny >= H) return -1;
    return ny * W + nx;
}
inline bool passable(int p) { return p >= 0 && cellType[p] != POND; }
// そのセル「から」出るのにかかるステップ数 / 燃料
inline int stepCost(int p, const vector<int>& status) {
    switch (cellType[p]) {
        case PLAIN: return 2;
        case MOUNTAIN: return 3;
        case ROAD: return status[p] == 0 ? 1 : status[p] == 1 ? 2 : 4;
    }
    return 1 << 20;
}
inline int fuelCost(int p) { return cellType[p] == PLAIN ? 1 : 2; }

// 道路状態: sumStay = 全チーム分の前日+前々日の滞在ステップ数
vector<int> statusFromStay(const vector<long long>& sumStay, int players) {
    vector<int> st(NC, 0);
    for (int p = 0; p < NC; p++) {
        if (cellType[p] != ROAD) continue;
        long long v = sumStay[p];
        st[p] = v < (long long)BUSY * players ? 0 : v < (long long)JAM * players ? 1 : 2;
    }
    return st;
}

// ------------------------------ シミュレータ（ルール完全再現） ------------------------------
struct Agent { int kind, pos, fuel; };  // kind 0 巡回車 / 1 補給車
struct DayResult {
    bool valid = true;
    string error;
    vector<Agent> end;
    vector<long long> stay;   // 自チームの道路セル滞在ステップ数
    vector<char> brandHit;    // [B]
    int brands = 0, balls = 0;
};

bool checkStructure(const vector<Agent>& st, const vector<vector<int>>& plan, const vector<int>& status, int steps, string& err) {
    if ((int)plan.size() != (int)st.size()) { err = "エージェント数が違う"; return false; }
    for (size_t i = 0; i < st.size(); i++) {
        int cur = st[i].pos; long long tot = 0;
        if (plan[i].empty()) { err = "agent " + to_string(i) + ": 行動が空"; return false; }
        for (int a : plan[i]) {
            if (a < 0) tot += -(long long)a;
            else if (a <= 5) {
                int nx = neighbor(cur, a);
                if (!passable(nx)) { err = "agent " + to_string(i) + ": 移動不可能なセルへ移動"; return false; }
                tot += stepCost(cur, status);
                cur = nx;
            } else { err = "agent " + to_string(i) + ": 不正な値 " + to_string(a); return false; }
        }
        if (tot != steps) { err = "agent " + to_string(i) + ": ステップ数 " + to_string(tot) + " != " + to_string(steps); return false; }
    }
    return true;
}

DayResult simulateDay(const vector<Agent>& st, const vector<vector<int>>& plan, const vector<int>& status, int steps) {
    DayResult r;
    r.stay.assign(NC, 0);
    r.brandHit.assign(B, 0);
    r.end = st;
    if (!checkStructure(st, plan, status, steps, r.error)) { r.valid = false; return r; }
    int n = st.size();
    vector<Agent> a = st;
    vector<int> ptr(n, 0), doneAt(n, INT_MAX), target(n, -1), fcost(n, 0), stock(S);
    for (int s = 0; s < S; s++) stock[s] = spots[s].stock;
    vector<vector<char>> got(n, vector<char>(S, 0));
    auto startAction = [&](int i, int t) {
        if (ptr[i] >= (int)plan[i].size()) { doneAt[i] = INT_MAX; target[i] = -1; return; }
        int x = plan[i][ptr[i]++];
        if (x < 0) { doneAt[i] = t - x; target[i] = -1; }
        else { target[i] = neighbor(a[i].pos, x); fcost[i] = fuelCost(a[i].pos); doneAt[i] = t + stepCost(a[i].pos, status); }
    };
    for (int i = 0; i < n; i++) startAction(i, 0);
    for (int t = 1; t <= steps; t++) {
        // 1. 燃料の消費
        for (int i = 0; i < n; i++)
            if (doneAt[i] == t && target[i] >= 0 && a[i].kind == 0) {
                if (a[i].fuel < fcost[i]) {
                    r.valid = false;
                    r.error = "agent " + to_string(i) + ": step " + to_string(t) + " で燃料不足";
                    r.end = st; r.stay.assign(NC, 0); r.brandHit.assign(B, 0); r.balls = 0;
                    return r;
                }
                a[i].fuel -= fcost[i];
            }
        // 2. 移動の反映
        for (int i = 0; i < n; i++) if (doneAt[i] == t && target[i] >= 0) a[i].pos = target[i];
        // 3. うどんの獲得（番号順）
        for (int i = 0; i < n; i++) {
            if (a[i].kind != 0) continue;
            int s = spotAt[a[i].pos];
            if (s >= 0 && !got[i][s] && stock[s] > 0) { got[i][s] = 1; stock[s]--; r.balls++; r.brandHit[spots[s].brand] = 1; }
        }
        // 4. 燃料の補給
        for (int i = 0; i < n; i++) {
            if (a[i].kind != 0) continue;
            for (int j = 0; j < n; j++) if (a[j].kind == 1 && a[j].pos == a[i].pos) { a[i].fuel = FUEL_LIMIT; break; }
        }
        // 5. 交通量の更新
        for (int i = 0; i < n; i++) if (cellType[a[i].pos] == ROAD) r.stay[a[i].pos]++;
        // アクションフェーズ
        if (t < steps) for (int i = 0; i < n; i++) if (doneAt[i] == t) startAction(i, t);
    }
    r.end = a;
    for (int b = 0; b < B; b++) r.brands += r.brandHit[b];
    return r;
}

vector<vector<int>> allWait(int steps) { return vector<vector<int>>(NA, vector<int>{-steps}); }

// ------------------------------ 経路 ------------------------------
struct PathTable { vector<int> t, f, par; };
struct Router {
    vector<int> status;
    unordered_map<int, PathTable> tbl[2];  // [0] 最速, [1] 燃料最小（要素の参照は再ハッシュでも無効にならない）
    explicit Router(const vector<int>& st) : status(st) {}
    const PathTable& get(int src, int mode) {
        auto it = tbl[mode].find(src);
        if (it != tbl[mode].end()) return it->second;
        PathTable pt;
        pt.t.assign(NC, INT_MAX); pt.f.assign(NC, INT_MAX); pt.par.assign(NC, -1);
        // 辞書順 (主キー, 副キー)
        using T = tuple<long long, int>;
        priority_queue<T, vector<T>, greater<T>> pq;
        auto key = [&](int t, int f) { return mode == 0 ? (long long)t * 100000 + f : (long long)f * 100000 + t; };
        pt.t[src] = 0; pt.f[src] = 0;
        pq.push({0, src});
        while (!pq.empty()) {
            auto [k, u] = pq.top(); pq.pop();
            if (k != key(pt.t[u], pt.f[u])) continue;
            int sc = stepCost(u, status), fc = fuelCost(u);
            for (int d = 0; d < 6; d++) {
                int v = neighbor(u, d);
                if (!passable(v)) continue;
                int nt = pt.t[u] + sc, nf = pt.f[u] + fc;
                if (pt.t[v] == INT_MAX || key(nt, nf) < key(pt.t[v], pt.f[v])) {
                    pt.t[v] = nt; pt.f[v] = nf; pt.par[v] = u;
                    pq.push({key(nt, nf), v});
                }
            }
        }
        return tbl[mode][src] = move(pt);
    }
    vector<int> path(int src, int dst, int mode) {  // src..dst のセル列
        const PathTable& pt = get(src, mode);
        vector<int> cells;
        for (int c = dst; c != -1; c = pt.par[c]) cells.push_back(c);
        reverse(cells.begin(), cells.end());
        return cells;
    }
};
int dirTo(int a, int b) {
    for (int d = 0; d < 6; d++) if (neighbor(a, d) == b) return d;
    return -1;
}
void appendMoves(const vector<int>& cells, vector<int>& acts) {
    for (size_t i = 0; i + 1 < cells.size(); i++) acts.push_back(dirTo(cells[i], cells[i + 1]));
}
int pathSteps(const vector<int>& cells, const vector<int>& status) {
    int t = 0;
    for (size_t i = 0; i + 1 < cells.size(); i++) t += stepCost(cells[i], status);
    return t;
}
// cells に沿って steps 以内で行けるところまでの行動
vector<int> movesWithin(const vector<int>& cells, const vector<int>& status, int steps, int& used) {
    vector<int> acts;
    used = 0;
    for (size_t k = 0; k + 1 < cells.size(); k++) {
        int c = stepCost(cells[k], status);
        if (used + c > steps) break;
        used += c;
        acts.push_back(dirTo(cells[k], cells[k + 1]));
    }
    return acts;
}

// ------------------------------ 入力 ------------------------------
void readProblem() {
    cin >> H >> W;
    NC = H * W;
    cellType.resize(NC);
    for (auto& c : cellType) cin >> c;
    cin >> S;
    spots.resize(S);
    vector<int> rawBrand(S);
    for (int i = 0; i < S; i++) cin >> rawBrand[i] >> spots[i].pos >> spots[i].stock;
    // 系列を 0..B-1 に振り直す
    vector<int> uniq = rawBrand;
    sort(uniq.begin(), uniq.end());
    uniq.erase(unique(uniq.begin(), uniq.end()), uniq.end());
    B = uniq.size();
    for (int i = 0; i < S; i++) spots[i].brand = lower_bound(uniq.begin(), uniq.end(), rawBrand[i]) - uniq.begin();
    spotAt.assign(NC, -1);
    for (int i = 0; i < S; i++) spotAt[spots[i].pos] = i;
    cin >> NA;
    agentStart.resize(NA);
    for (auto& a : agentStart) cin >> a;
    cin >> FUEL_LIMIT >> D;
    daySteps.resize(D); daySeconds.resize(D);
    for (auto& x : daySteps) cin >> x;
    for (auto& x : daySeconds) cin >> x;
    cin >> PLAYERS >> BUSY >> JAM;
}
vector<int> readStatus() {
    int T; cin >> T;
    vector<int> st(NC, 0);
    for (int i = 0; i < T; i++) { int p, s; cin >> p >> s; st[p] = s; }
    return st;
}

// ------------------------------ 各 solver が定義するもの ------------------------------
vector<vector<int>> planDay(const vector<Agent>& st, const vector<int>& status, const vector<char>& before,
                            int steps, bool lastDay, double timeMs);
vector<vector<int>> kindCandidates();

// ------------------------------ KIND ------------------------------
struct MatchScore {
    int matchBrands = 0, dayBrands = 0, balls = 0;
    bool operator<(const MatchScore& o) const {
        return tie(matchBrands, dayBrands, balls) < tie(o.matchBrands, o.dayBrands, o.balls);
    }
};

// 全チームが同じ動きをすると仮定（交通量 = 自チームの滞在数）して全日程を回す
MatchScore simulateMatch(const vector<int>& kinds, double timeMs) {
    vector<Agent> st(NA);
    for (int i = 0; i < NA; i++) st[i] = {kinds[i], agentStart[i], FUEL_LIMIT};
    vector<char> collected(B, 0);
    vector<vector<long long>> stays;
    MatchScore ms;
    for (int d = 0; d < D; d++) {
        vector<long long> sum(NC, 0);
        for (int k = max(0, d - 2); k < d; k++) for (int p = 0; p < NC; p++) sum[p] += stays[k][p];
        vector<int> status = statusFromStay(sum, 1);
        auto plan = planDay(st, status, collected, daySteps[d], d == D - 1, timeMs / D);
        DayResult r = simulateDay(st, plan, status, daySteps[d]);
        if (!r.valid) r = simulateDay(st, allWait(daySteps[d]), status, daySteps[d]);
        for (int b = 0; b < B; b++) collected[b] |= r.brandHit[b];
        ms.dayBrands += r.brands; ms.balls += r.balls;
        stays.push_back(r.stay);
        st = r.end;
    }
    for (int b = 0; b < B; b++) ms.matchBrands += collected[b];
    return ms;
}

vector<int> solveKind(double timeMs) {
    auto cands = kindCandidates();
    Timer tm;
    MatchScore best; vector<int> bestK = cands[0];
    for (size_t c = 0; c < cands.size(); c++) {
        double remain = timeMs - tm.ms();
        if (c > 0 && remain <= 0) { cerr << "[kind] 時間切れ: 残り " << cands.size() - c << " 候補を省略\n"; break; }
        MatchScore ms = simulateMatch(cands[c], max(1.0, remain / (cands.size() - c)));
        cerr << "[kind] cand=" << c << " (";
        for (int k : cands[c]) cerr << k;
        cerr << ") brands=" << ms.matchBrands << " dayBrands=" << ms.dayBrands << " balls=" << ms.balls << "\n";
        if (c == 0 || best < ms) { best = ms; bestK = cands[c]; }
    }
    return bestK;
}

// ------------------------------ 共通の main ------------------------------
int solverMain() {
    readProblem();
    string mode; double timeLimit;
    cin >> mode >> timeLimit;
    Timer total;

    if (mode == "KIND") {
        auto kinds = solveKind(timeLimit * 0.8);
        for (int i = 0; i < NA; i++) cout << kinds[i] << (i + 1 < NA ? ' ' : '\n');
        return 0;
    }

    // ---- DAY ----
    int day; cin >> day;
    vector<Agent> st(NA);
    for (auto& a : st) cin >> a.kind >> a.pos >> a.fuel;
    vector<int> status = readStatus();
    int P; cin >> P;  // 他チーム（今は未使用）
    for (int k = 0; k < P; k++) { int id; cin >> id; for (int i = 0; i < NA; i++) { int x, y, z; cin >> x >> y >> z; } }

    // 過去の日を再生して、取った系列を復元する
    vector<Agent> rep(NA);
    for (int i = 0; i < NA; i++) rep[i] = {st[i].kind, agentStart[i], FUEL_LIMIT};
    vector<char> collected(B, 0);
    for (int k = 0; k < day; k++) {
        vector<int> stk = readStatus();
        vector<vector<int>> plan(NA);
        for (int i = 0; i < NA; i++) { int len; cin >> len; plan[i].resize(len); for (auto& x : plan[i]) cin >> x; }
        DayResult r = simulateDay(rep, plan, stk, daySteps[k]);
        if (!r.valid) r = simulateDay(rep, allWait(daySteps[k]), stk, daySteps[k]);
        for (int b = 0; b < B; b++) collected[b] |= r.brandHit[b];
        rep = r.end;
    }
    for (int i = 0; i < NA; i++)
        if (rep[i].pos != st[i].pos || (st[i].kind == 0 && rep[i].fuel != st[i].fuel))
            cerr << "[warn] 再生結果とサーバーの状態が違う agent " << i << " sim(pos=" << rep[i].pos << ",fuel=" << rep[i].fuel
                 << ") server(pos=" << st[i].pos << ",fuel=" << st[i].fuel << ")\n";

    double remain = timeLimit - total.ms();
    auto plan = planDay(st, status, collected, daySteps[day], day == D - 1, remain * 0.85);
    DayResult r = simulateDay(st, plan, status, daySteps[day]);
    cerr << "[day " << day << "] brands=" << r.brands << " balls=" << r.balls << (r.valid ? "" : " INVALID " + r.error) << "\n";
    for (auto& a : plan) {
        for (size_t i = 0; i < a.size(); i++) cout << a[i] << (i + 1 < a.size() ? ' ' : '\n');
    }
    return 0;
}
