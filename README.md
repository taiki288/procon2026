# 高専プロコン2026 競技部門

```
solvers/              解答（common.hpp + 方式ごとの .cpp）。通信なし、標準入力 → 標準出力
tools/
  judge.js            ローカルジャッジ（サーバーなしで seed ごとに対戦 → 結果 JSON）
  gen.py              テスト用マップを seed ごとに生成（maps/gen/）
  client.py           サーバー通信。solver を呼んで提出し、ログを保存
  run_local.py        公式の簡易サーバー + 複数 client でローカル対戦（最終確認用）
  make_input.py       サーバーなしで solver を試す入力を作る
  procon_io.py        JSON ⇔ solver 入力の変換（共通）
visualizer/
  index.html          ビジュアライザ（ブラウザで開いて JSON をドロップ）
  sim.js              シミュレータ（visualizer と CLI のスコア計算で共通）
maps/                 公式の参考マップ（sample_16/24/32）と試合設定の例、生成マップ gen/
server/               公式の簡易サーバー（Windows 版）と API 仕様書 api.html
logs/                 対戦ログ（自動生成）
```

## はじめに（clone したあと）

公式サイトの配布物はリポジトリに入れていないので、最初に取得します。

```bash
python tools/fetch_official.py
```

- `server/` に簡易サーバー（Windows 版）・API 仕様書 `api.html`、`maps/` に参考マップ 3 枚と試合設定の例が入ります
- 取得元: [高専プロコン公式サイト](https://www.procon.gr.jp/) の「回答システムに関する情報と簡易版の回答用サーバー」（8/20）と「本選マップに関するお知らせ」（9/18）
- macOS / Linux 版の簡易サーバーも同じ zip に入っています（必要なら手で取り出してください）
- 必要なもの: g++（C++17）、Python 3、Node.js

## 複数の solver を比べる（seed ごと）

```
solvers/
  common.hpp     共通部分（シミュレータ・経路・入力・KIND/DAY の流れ）
  escort.cpp     随伴方式: 補給車が巡回車 1 台にくっついて動く
  hub.cpp        拠点方式: 補給車は毎朝拠点へ移動して待ち、巡回車がそこへ寄って満タンにする
```

```bash
node tools/judge.js solvers/escort.cpp solvers/hub.cpp --seeds 0-99
```

- サーバーを使わず `visualizer/sim.js`（簡易サーバーと一致を確認済み）で試合を回す。seed 100 個で数分
- `maps/gen/NNNN.json` がなければ `tools/gen.py` で自動生成（サイズは seed % 3 で 16 / 24 / 32）
- `.cpp` は `build/` に自動でビルド（`common.hpp` を変えたら再ビルド）
- 出力: `logs/月日-時分-チーム数teams/NNNN.json`（例: `logs/0928-1715-3teams/`）（seed ごと）、`summary.json`、`stderr/`（solver のログ）
- 主なオプション: `--time-ms 1000`（1 日の持ち時間）、`--copies 2`（各 solver 2 チーム）、
  `--maps maps/sample_16.json,...`（マップ直接指定）、`--parallel 8`
- 新しい solver は `common.hpp` を include して `planDay` と `kindCandidates` を書き、`main` で `solverMain()` を呼ぶだけ

結果はビジュアライザの「📁 フォルダを開く」で出力フォルダを選ぶと、seed とチームを切り替えて見られます。

## ビルド

judge は自動でビルドします。手でビルドするとき:

```bash
g++ -std=c++17 -O2 -o build/hub.exe solvers/hub.cpp
```

## ローカル対戦（公式の簡易サーバーを使う）

```bash
python tools/run_local.py --map maps/sample_16.json --teams 4 --day-seconds 10
```

- 終わると `logs/月日-時分-チーム数teams/vis.json` ができ、スコア表が出ます（node が必要）
- solver を指定: `--solvers build/hub.exe,build/escort.exe`（既定は全チーム `build/hub.exe`）
- 各日の回答時間は `--day-seconds` で短くできます（本番は 60 秒）

## 本番

```bash
python tools/client.py --url http://<サーバー> --token <トークン> --solver build/hub.exe --kind-time-ms 50000
```

- `--kind-time-ms` : 種別を決めるのに solver へ渡す時間（1回戦 60 秒 / 準決勝 90 秒 / 決勝 120 秒 なので余裕をもって）
- `--margin` : 締切の何秒前に solver を打ち切るか（既定 3 秒）
- 毎日まず「全員待機」を提出してから solver の回答を出し直します（却下・時間切れ対策）
- ログは `logs/<token>.json`。そのまま visualizer で開けます（自チームのみ。道路状態はサーバーの値を使います）

## ビジュアライザ

`visualizer/index.html` をブラウザで開き、JSON をドロップ（またはファイル選択）。

- `vis.json`（ローカル対戦）/ client のログ / 参考マップ（マップだけ見る）を読めます
- **「📁 フォルダを開く」で judge の出力フォルダを読むと、試合(seed) を ◀ ▶ / `[` `]` キーで切り替え**。
  チームを選んだまま seed を移れます。右上に solver ごとの集計（1 位の回数・平均順位など）と試合一覧
- 同じマップのファイルは 1 試合にまとめ（例: client の `team0.json` と `team1.json`）、違うマップは別の試合になります。
  「＋ 追加」ボタンか Shift+ドロップで、今の表示にファイルを足せます
- 試合の全チームがそろえば渋滞を自前で計算、足りなければサーバーが配った道路状態を使います
- 日・ステップのスライダー、再生、← → キー、スペースで再生/停止
- 右側: 順位表（クリックでチーム切替）、日別成績、エージェントの燃料、系列の取得状況
- セルにマウスを乗せると地形・道路状態・交通量・スポット・いるエージェントを表示
- ログにサーバーの状態が入っていれば、シミュレーション結果との食い違いを赤字で表示

コマンドラインでスコアだけ見る:

```bash
node visualizer/sim.js logs/0928-1715-2teams/vis.json
```

## solver の入出力

### 入力（`tools/procon_io.py` の `build_input` が生成）

```
H W
cells: H 行 × W 個       0 平地 / 1 道路 / 2 山地 / 3 池
S
S 行: brand pos stocks
NA
NA 個: エージェント初期位置
fuelLimit
D
D 個: daySteps
D 個: daySeconds
players busyThreshold jammedThreshold
MODE                     KIND または DAY
timeLimitMs              solver が使ってよい時間
--- 以下 DAY のみ ---
day                      今日（0 始まり）
NA 行: kind pos fuel     今日の開始時の自チーム
T  / T 行: pos status    今日の道路状態
P  / P 回: id と NA 行の kind pos fuel   他チーム
過去の日ごと（day 回）:
  T / T 行: pos status   その日の道路状態
  NA 行: len a1 ... alen 実際に採用された行動
```

### 出力

- KIND: `0 1 0 0`（NA 個、0 巡回車 / 1 補給車）
- DAY : エージェントごとに 1 行、行動を空白区切り（`-k` = k ステップ待機、`0..5` = 移動方向）

方向は 0 左上 / 1 右上 / 2 右 / 3 右下 / 4 左下 / 5 左。`pos = y*W + x`、偶数行が右にずれる。
（簡易サーバーとの照合で、位置・燃料・道路状態が一致することを確認済み）
