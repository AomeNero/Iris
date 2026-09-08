#!/usr/bin/env python3
"""VirusTotal 自动扫描：上传文件 → 轮询分析 → 汇总报毒引擎。
用法：VT_API_KEY=xxx python scripts/vt_scan.py [文件路径]
API key 仅从环境变量 VT_API_KEY 读取，绝不硬编码/提交仓库。"""
import os
import sys
import time
import json
import hashlib
import mimetypes
import urllib.request
import urllib.error
from io import BytesIO

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

API_BASE = "https://www.virustotal.com/api/v3"


def http(method, path, api_key, form=None):
    url = API_BASE + path
    headers = {"x-apikey": api_key, "Accept": "application/json"}
    data = None
    if form is not None:
        boundary = "----vt" + hashlib.md5(str(time.time()).encode()).hexdigest()
        buf = BytesIO()
        for name, (filename, fdata) in form.items():
            buf.write(f"--{boundary}\r\n".encode())
            buf.write(
                f'Content-Disposition: form-data; name="{name}"; filename="{filename}"\r\n'.encode()
            )
            ctype = mimetypes.guess_type(filename)[0] or "application/octet-stream"
            buf.write(f"Content-Type: {ctype}\r\n\r\n".encode())
            buf.write(fdata)
            buf.write(b"\r\n")
        buf.write(f"--{boundary}--\r\n".encode())
        data = buf.getvalue()
        headers["Content-Type"] = f"multipart/form-data; boundary={boundary}"

    for attempt in range(4):
        req = urllib.request.Request(url, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return json.loads(r.read().decode("utf-8", "replace"))
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", "replace")
            if e.code == 429 and attempt < 3:  # 免费版速率限制：等 60s 重试
                print("  速率限制(429)，等 60s 重试…", file=sys.stderr)
                time.sleep(60)
                continue
            print(f"HTTP {e.code}: {body[:400]}", file=sys.stderr)
            sys.exit(2)
        except Exception as e:
            if attempt < 3:
                print(f"  网络错误 {e}，重试…", file=sys.stderr)
                time.sleep(15)
                continue
            raise


def main():
    api_key = os.environ.get("VT_API_KEY")
    if not api_key:
        print("ERROR: 请设置环境变量 VT_API_KEY", file=sys.stderr)
        sys.exit(1)
    path = sys.argv[1] if len(sys.argv) > 1 else "build/Release/Iris.exe"
    if not os.path.isfile(path):
        print(f"ERROR: 文件不存在: {path}", file=sys.stderr)
        sys.exit(1)

    with open(path, "rb") as f:
        fdata = f.read()
    sha256 = hashlib.sha256(fdata).hexdigest()
    print(f"扫描: {path} ({len(fdata)/1048576:.1f} MB, sha256={sha256[:16]}…)")

    # 1. 上传
    print("上传中…")
    r = http("POST", "/files", api_key, form={"file": (os.path.basename(path), fdata)})
    aid = r["data"]["id"]
    print(f"已上传，analysis id: {aid}")

    # 2. 轮询（30s 间隔，避开免费版 4 次/分钟限制；最多 ~6 分钟）
    for i in range(12):
        time.sleep(30)
        r = http("GET", f"/analyses/{aid}", api_key)
        st = r["data"]["attributes"]["status"]
        print(f"  [{i+1}/12] status: {st}")
        if st == "completed":
            break
    else:
        print("分析未在限时内完成，稍后访问报告链接查看", file=sys.stderr)

    # 3. 拉文件报告（按 sha256 取完整各引擎结果）
    r = http("GET", f"/files/{sha256}", api_key)
    attrs = r["data"]["attributes"]
    results = attrs.get("last_analysis_results", {})
    stats = attrs.get("last_analysis_stats", {})
    flagged = sorted(
        (n, v.get("result") or "(无检测名)")
        for n, v in results.items()
        if v.get("category") == "malicious"
    )

    # 4. 汇总
    print("\n" + "=" * 64)
    print(f"结果: {len(flagged)} / {len(results)} 引擎报毒   stats={stats}")
    print("=" * 64)
    if flagged:
        for n, res in flagged:
            print(f"  {n:<24} {res}")
    else:
        print("✅ 无引擎报毒")
    print(f"\nreputation: {attrs.get('reputation')}")
    print(f"报告: https://www.virustotal.com/gui/file/{sha256}")


if __name__ == "__main__":
    main()
