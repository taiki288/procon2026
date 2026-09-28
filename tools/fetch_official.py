#!/usr/bin/env python3
"""
公式サイトの配布物をダウンロードして所定の場所に置く（リポジトリには含めていないもの）。

  python tools/fetch_official.py

  server/  … 簡易サーバー（Windows 版）, README.md, CREDITS.txt, API 仕様書 api.html
  maps/    … 参考マップ sample_16/24/32.json, 試合設定の例 example_config.json と説明 config_format.md
"""
import io
import os
import sys
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# 公式サイト https://www.procon.gr.jp/ のお知らせからリンクされているファイル
ANSWER_SYSTEM = "https://www.procon.gr.jp/uploads/media/BecQtyfAVAA"  # 回答システムに関する情報・簡易版の回答用サーバー（8/20）
SAMPLE_MAPS = "https://www.procon.gr.jp/uploads/media/BfCOVgvgVAA"    # 参考マップ（9/18）


def name_of(info):
    n = info.filename
    if not info.flag_bits & 0x800:  # UTF-8 フラグなし → Windows の日本語ファイル名
        try:
            n = n.encode("cp437").decode("cp932")
        except UnicodeError:
            pass
    return n


def entries(data):
    """zip（入れ子の zip も展開）の中身を (ファイル名, バイト列) で返す"""
    z = zipfile.ZipFile(io.BytesIO(data))
    for info in z.infolist():
        if info.is_dir():
            continue
        n, body = name_of(info), z.read(info)
        if n.endswith(".zip"):
            yield from entries(body)
        else:
            yield os.path.basename(n), body


def download(url):
    print(f"download {url}")
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def save(path, body):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(body)
    print(f"  -> {os.path.relpath(path, ROOT)}")


def main():
    for name, body in entries(download(ANSWER_SYSTEM)):
        if name in ("procon-server-windows-amd64.exe", "README.md", "CREDITS.txt"):
            save(os.path.join(ROOT, "server", name), body)
        elif name == "redoc-static.html":
            save(os.path.join(ROOT, "server", "api.html"), body)
        elif name == "example.json":
            save(os.path.join(ROOT, "maps", "example_config.json"), body)
        elif name.endswith(".md") and "JSON" in name:
            save(os.path.join(ROOT, "maps", "config_format.md"), body)
    for name, body in entries(download(SAMPLE_MAPS)):
        if name.startswith("sample_") and name.endswith(".json"):
            save(os.path.join(ROOT, "maps", name), body)
    print("完了")
    return 0


if __name__ == "__main__":
    sys.exit(main())
