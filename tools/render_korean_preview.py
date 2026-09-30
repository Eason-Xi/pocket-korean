#!/usr/bin/env python3
"""在主机上用真实的 LVGL + 应用界面代码渲染每个页面，输出 PNG 和 LVGL 内存池占用。

这是开发辅助，不进校验门，也不参与固件构建。它能替你发现：
  * 缺字形（预览里会出现占位框）、文字被裁切 / 重叠、版面放不下；
  * 每个页面创建后 LVGL 内置内存池（固件里只有 24 KB）用了多少。
它不能替代真机：屏幕的真实色彩、刷新与触感只有上板才知道。

依赖：C 编译器、Python 3 标准库；LVGL 源码来自 idf.py / validate.sh --firmware 下载到
managed_components/ 里的那份（版本由 dependencies.lock 锁定）。

用法（仓库根目录）：
  python3 tools/render_korean_preview.py [--out build/preview] [--scale 2]
  python3 tools/render_korean_preview.py --mem-kb 48      # 试试更大的内存池
  峰值超过池的 75%（--budget）时脚本会失败：LVGL 池耗尽在板上表现为卡死 / 白屏。
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import os
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LVGL_DIR = ROOT / "managed_components" / "lvgl__lvgl"
PREVIEW_DIR = ROOT / "tools" / "korean_preview"
FONT_SOURCES = sorted((ROOT / "assets" / "fonts").glob("ko_*.c"))
APP_SOURCES = [
    "ko_content.c", "ko_content_gen.c", "ko_progress.c", "ko_crc32.c", "ko_quiz.c", "ko_rng.c",
    "ko_layout.c", "ko_session.c", "ko_view.c", "ko_nav.c",
] + [p.name for p in sorted((ROOT / "main").glob("ko_ui*.c"))] + ["ko_fonts.c"]
SCREEN_W, SCREEN_H, CORNER_RADIUS = 240, 320, 30


def compile_flags(mem_kb: int) -> list[str]:
    return [
        "-std=gnu11", "-O1", "-g0", "-w",
        f"-DLV_CONF_PATH=\"{PREVIEW_DIR / 'lv_conf.h'}\"", f"-DKO_PREVIEW_MEM_KB={mem_kb}",
        f"-I{LVGL_DIR}", f"-I{LVGL_DIR / 'src'}", f"-I{ROOT / 'main'}", f"-I{PREVIEW_DIR}",
    ]


def compile_one(source: Path, obj: Path, flags: list[str], cache: bool) -> None:
    # 只缓存 LVGL：它的源码固定不变。应用代码依赖一堆头文件（ko_strings.h、字体声明……），
    # 按单个 .c 的修改时间缓存会让改了头文件的对象悄悄过期，预览就会"看着对、其实是旧代码"。
    if cache and obj.exists() and obj.stat().st_mtime >= source.stat().st_mtime:
        return
    obj.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["cc", *flags, "-c", str(source), "-o", str(obj)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"ERROR compiling {source}:\n{result.stderr}")


def build(out: Path, mem_kb: int) -> Path:
    if not LVGL_DIR.is_dir():
        raise SystemExit("ERROR: managed_components/lvgl__lvgl not found; run "
                         "./tools/validate.sh --firmware once to download the pinned LVGL")
    flags = compile_flags(mem_kb)
    # LVGL 的对象文件依赖 lv_conf.h：把它的哈希放进目录名，配置一改缓存就自动作废，
    # 否则会拿旧配置编出的对象去测内存，得出看似合理、其实过期的数字。
    conf_hash = hashlib.sha256((PREVIEW_DIR / "lv_conf.h").read_bytes()).hexdigest()[:8]
    obj_root = out / f"obj{mem_kb}-{conf_hash}"

    # 全部编进来：可选模块（驱动、第三方库、其他 OS 抽象）都由 lv_conf.h 里的宏关闭，
    # 编出来是空目标文件；手工挑文件反而容易漏掉 lv_os.c / bin_decoder 这类必需项。
    lvgl_sources = sorted((LVGL_DIR / "src").rglob("*.c"))
    app_sources = [ROOT / "main" / name for name in APP_SOURCES] + FONT_SOURCES
    app_sources.append(PREVIEW_DIR / "preview_main.c")

    jobs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for source in lvgl_sources + app_sources:
            obj = obj_root / (str(source.relative_to(ROOT)).replace("/", "__") + ".o")
            # 应用代码用严格警告，LVGL 与生成的字体只要能编译。
            these = flags
            is_lvgl = LVGL_DIR in source.parents
            if source.parent == ROOT / "main":
                these = [f for f in flags if f != "-w"] + ["-Wall", "-Wextra", "-Werror"]
            jobs.append(pool.submit(compile_one, source, obj, these, is_lvgl))
        for job in jobs:
            job.result()

    binary = out / f"preview{mem_kb}"
    objs = [str(p) for p in obj_root.rglob("*.o")]
    link = subprocess.run(["cc", *objs, "-o", str(binary), "-lm"], capture_output=True, text=True)
    if link.returncode != 0:
        raise SystemExit(f"ERROR linking:\n{link.stderr}")
    return binary


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    assert match, path
    return int(match.group(1)), int(match.group(2)), data[match.end():]


def mask_corners(rgb: bytearray, width: int, height: int, radius: int) -> None:
    """复现 BSP 的圆角涂黑（BSP_LVGL_SCREEN_RADIUS）：预览才能看出内容会不会被角落吃掉。"""
    r2 = radius * radius
    for cy, cx, ys, xs in ((radius, radius, range(0, radius), range(0, radius)),
                           (radius, width - radius, range(0, radius), range(width - radius, width)),
                           (height - radius, radius, range(height - radius, height), range(0, radius)),
                           (height - radius, width - radius, range(height - radius, height),
                            range(width - radius, width))):
        for y in ys:
            for x in xs:
                if (x - cx) ** 2 + (y - cy) ** 2 > r2 and abs(x - cx) < radius and abs(y - cy) < radius:
                    i = (y * width + x) * 3
                    rgb[i:i + 3] = b"\x00\x00\x00"


def scale_up(rgb: bytes, width: int, height: int, factor: int) -> tuple[int, int, bytes]:
    if factor == 1:
        return width, height, rgb
    out = bytearray()
    for y in range(height):
        row = bytearray()
        for x in range(width):
            row += rgb[(y * width + x) * 3:(y * width + x) * 3 + 3] * factor
        out += bytes(row) * factor
    return width * factor, height * factor, bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(ROOT / "build" / "preview"))
    parser.add_argument("--scale", type=int, default=2, help="PNG upscale factor")
    parser.add_argument("--mem-kb", type=int, default=32, help="LVGL pool size (firmware: sdkconfig.defaults)")
    parser.add_argument("--budget", type=float, default=0.75, help="max allowed peak as a fraction of the pool")
    parser.add_argument("--timeout", type=int, default=60, help="seconds to allow the preview program")
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    binary = build(out, args.mem_kb)

    shots = out / "shots"
    shots.mkdir(exist_ok=True)
    for stale in shots.glob("*.ppm"):
        stale.unlink()
    try:
        result = subprocess.run([str(binary), str(shots)], capture_output=True, text=True,
                                timeout=args.timeout)
    except subprocess.TimeoutExpired as expired:
        partial = expired.stdout.decode() if isinstance(expired.stdout, bytes) else (expired.stdout or "")
        sys.stdout.write(partial)
        raise SystemExit(f"ERROR: preview program did not finish within {args.timeout} s (hang?)")
    sys.stdout.write(result.stdout)
    peak = re.search(r"PEAK used=(\d+) of (\d+)", result.stdout)
    if peak and int(peak.group(1)) > args.budget * int(peak.group(2)):
        raise SystemExit(f"ERROR: LVGL pool peak {peak.group(1)} B exceeds {args.budget:.0%} of "
                         f"{peak.group(2)} B; raise CONFIG_LV_MEM_SIZE_KILOBYTES or slim the UI")
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit(f"ERROR: preview program failed ({result.returncode})")

    for ppm in sorted(shots.glob("*.ppm")):
        width, height, rgb = read_ppm(ppm)
        buf = bytearray(rgb)
        mask_corners(buf, width, height, CORNER_RADIUS)
        w2, h2, scaled = scale_up(bytes(buf), width, height, args.scale)
        write_png(ppm.with_suffix(".png"), w2, h2, scaled)
        ppm.unlink()
    print(f"wrote {len(list(shots.glob('*.png')))} PNG file(s) to {shots}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
