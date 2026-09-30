#!/usr/bin/env python3
"""韩语学习应用的发音工具：云端合成 → 后处理 → ADPCM 编码 → 打成 kopack 分区镜像。

流程（每一步都可单独重跑，已有结果会被复用）：

  1. synth   用 Microsoft Edge 在线朗读（edge-tts，韩语神经语音）逐条合成 content.json 里
             需要发音的韩文，存到 <workdir>/raw/<语音+语速+文本的哈希>.mp3。按文本哈希缓存：
             内容增删、clip 序号变化都不会让已有录音对错号，也不会重复请求。
  2. pack    用 ffmpeg 转成 16 kHz 单声道 → 去首尾静音 → 音量归一 → IMA-ADPCM 编码，
             打成 assets/audio/generated/kopack.bin（烧到 kopack 分区）。
  3. verify  重新解析 kopack.bin：头 / 索引 CRC、逐条解码，打印统计。

  synthetic  生成"纯提示音"的发音包（不是真发音），用来在不联网时验证整条播放链路。
  fixture    生成主机测试用的小型合成发音包头文件（不需要 ffmpeg 和网络）。

凭据：Edge TTS 不需要 API Key；synth 需要能访问 Microsoft 的在线朗读服务。

授权提醒：生成的音频来自第三方在线 TTS 服务，能否随固件再分发取决于服务条款；
     因此 assets/audio/generated/ 默认不入库（见 .gitignore），确认授权后再决定是否提交。

依赖：Python 3 标准库；synth 需要 `pip install edge-tts`（固定用 7.x 测过）；
     pack 需要 PATH 中有 ffmpeg（也可用环境变量 FFMPEG 指定）。
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import hashlib
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import korean_content as kc  # noqa: E402

ROOT = kc.ROOT
DEFAULT_WORKDIR = ROOT / "build" / "korean-audio"
DEFAULT_PACK = ROOT / "assets" / "audio" / "generated" / "kopack.bin"

SAMPLE_RATE = 16000
PACK_VERSION = 1
HEADER_BYTES = 40
ENTRY_BYTES = 12

# Edge TTS 的韩语语音：ko-KR-SunHiNeural（女）、ko-KR-InJoonNeural（男）、ko-KR-HyunsuMultilingualNeural。
# 学习用发音略放慢一点（-10%），初学者更容易听清每个音节。
DEFAULT_VOICE = "ko-KR-SunHiNeural"
DEFAULT_RATE = "-10%"

# --------------------------------------------------------------------------
# IMA-ADPCM（与固件 main/ko_adpcm.c 逐行对应，也与 ffmpeg adpcm_ima_wav 逐点一致；两边任何一处改动都要同步）
# --------------------------------------------------------------------------

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253,
    279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
]
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def adpcm_step(predictor: int, index: int, nibble: int) -> tuple[int, int]:
    step = STEP_TABLE[index]
    # 与 ffmpeg adpcm_ima_wav 相同的一次性公式；教科书的分项移位写法会有 1~3 个最低位的舍入差。
    diff = ((2 * (nibble & 7) + 1) * step) >> 3
    predictor += -diff if nibble & 8 else diff
    predictor = max(-32768, min(32767, predictor))
    index = max(0, min(88, index + INDEX_TABLE[nibble]))
    return predictor, index


def initial_index(samples: list[int]) -> int:
    """按第一步的变化幅度选起始步长，避免响亮的开头被过小的步长"追"很多个采样。"""
    if len(samples) < 2:
        return 0
    delta = abs(samples[1] - samples[0])
    best = 0
    for i, step in enumerate(STEP_TABLE):
        if step * 4 <= max(delta, 1):
            best = i
    return best


def adpcm_encode(samples: list[int]) -> bytes:
    """返回 4 字节头 + 码流。samples[0] 存在头的预测值里，其余每个采样编成一个 4 bit 码。"""
    if not samples:
        raise ValueError("cannot encode an empty clip")
    predictor = max(-32768, min(32767, samples[0]))
    index = initial_index(samples)
    header = struct.pack("<hBB", predictor, index, 0)

    nibbles = []
    for sample in samples[1:]:
        step = STEP_TABLE[index]
        diff = sample - predictor
        nibble = 0
        if diff < 0:
            nibble = 8
            diff = -diff
        if diff >= step:
            nibble |= 4
            diff -= step
        step >>= 1
        if diff >= step:
            nibble |= 2
            diff -= step
        step >>= 1
        if diff >= step:
            nibble |= 1
        # 用与解码器完全相同的函数推进状态，编码端与解码端才不会漂移。
        predictor, index = adpcm_step(predictor, index, nibble)
        nibbles.append(nibble)

    body = bytearray()
    for i in range(0, len(nibbles), 2):
        low = nibbles[i]
        high = nibbles[i + 1] if i + 1 < len(nibbles) else 0
        body.append(low | (high << 4))
    return header + bytes(body)


def adpcm_decode(data: bytes, samples: int) -> list[int]:
    predictor, index, _ = struct.unpack("<hBB", data[:4])
    index = min(index, 88)
    out = [predictor]
    codes = samples - 1
    for i in range(codes):
        byte = data[4 + (i >> 1)]
        nibble = (byte >> 4) if i & 1 else (byte & 0x0F)
        predictor, index = adpcm_step(predictor, index, nibble)
        out.append(predictor)
    return out


# --------------------------------------------------------------------------
# kopack 分区镜像（格式见 main/ko_pack.h）
# --------------------------------------------------------------------------

def build_pack(clips: list[tuple[bytes, int] | None]) -> bytes:
    """clips[i] = (ADPCM 流, 采样数)；缺失的用 None。"""
    count = len(clips)
    index_offset = HEADER_BYTES
    data_offset = index_offset + count * ENTRY_BYTES

    entries = bytearray()
    data = bytearray()
    for clip in clips:
        if clip is None:
            entries += struct.pack("<III", 0, 0, 0)
            continue
        stream, samples = clip
        entries += struct.pack("<III", len(data), len(stream), samples)
        data += stream
        data += b"\x00" * (-len(data) % 4)   # 每条按 4 字节对齐，读 Flash 更整齐

    total = data_offset + len(data)
    header = struct.pack(
        "<4sHHIIIIII", b"KOPK", PACK_VERSION, HEADER_BYTES, count, SAMPLE_RATE,
        index_offset, data_offset, total, zlib_crc32(bytes(entries)),
    ) + struct.pack("<I", 0)
    header += struct.pack("<I", zlib_crc32(header))
    assert len(header) == HEADER_BYTES
    return header + bytes(entries) + bytes(data)


def zlib_crc32(data: bytes) -> int:
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_pack(blob: bytes) -> dict:
    """解析并校验 kopack；不合法时抛 ValueError。"""
    if len(blob) < HEADER_BYTES:
        raise ValueError("pack shorter than its header")
    (magic, version, header_size, count, rate, index_offset, data_offset, total,
     index_crc, _reserved, header_crc) = struct.unpack("<4sHHIIIIIIII", blob[:HEADER_BYTES])
    if magic != b"KOPK":
        raise ValueError("bad magic")
    if version != PACK_VERSION or header_size != HEADER_BYTES:
        raise ValueError("unsupported version / header size")
    if zlib_crc32(blob[:36]) != header_crc:
        raise ValueError("header CRC mismatch")
    if rate != SAMPLE_RATE:
        raise ValueError(f"unsupported sample rate {rate}")
    if index_offset != HEADER_BYTES or data_offset != index_offset + count * ENTRY_BYTES:
        raise ValueError("inconsistent offsets")
    if total > len(blob) or total < data_offset:
        raise ValueError("total_size does not fit the file")
    index = blob[index_offset:data_offset]
    if zlib_crc32(index) != index_crc:
        raise ValueError("index CRC mismatch")

    clips = []
    for i in range(count):
        offset, size, samples = struct.unpack_from("<III", index, i * ENTRY_BYTES)
        if size == 0:
            clips.append(None)
            continue
        if offset + size > total - data_offset or size < 5 or not 1 <= samples <= 1 + (size - 4) * 2:
            raise ValueError(f"clip {i}: entry out of bounds")
        clips.append({"offset": data_offset + offset, "bytes": size, "samples": samples})
    return {"clip_count": count, "total_size": total, "clips": clips}


# --------------------------------------------------------------------------
# 音频后处理（纯函数，便于测试）
# --------------------------------------------------------------------------

def trim_silence(pcm: list[int], rate: int = SAMPLE_RATE, threshold_ratio: float = 0.02,
                 lead_ms: int = 40, tail_ms: int = 90) -> list[int]:
    """去掉首尾静音，前后各留一点余量避免吃字。全静音时原样返回。"""
    peak = max((abs(s) for s in pcm), default=0)
    if peak == 0:
        return pcm
    threshold = max(int(peak * threshold_ratio), 1)
    first = next(i for i, s in enumerate(pcm) if abs(s) >= threshold)
    last = len(pcm) - 1 - next(i for i, s in enumerate(reversed(pcm)) if abs(s) >= threshold)
    start = max(0, first - rate * lead_ms // 1000)
    end = min(len(pcm), last + 1 + rate * tail_ms // 1000)
    return pcm[start:end]


def normalize(pcm: list[int], target_rms: float = 3200.0, peak_limit: float = 30000.0) -> list[int]:
    """把整体响度拉到统一水平（否则不同条目音量忽大忽小），同时保证不削波。"""
    if not pcm:
        return pcm
    rms = math.sqrt(sum(s * s for s in pcm) / len(pcm))
    peak = max(abs(s) for s in pcm)
    if rms == 0 or peak == 0:
        return pcm
    gain = min(target_rms / rms, peak_limit / peak)
    return [max(-32768, min(32767, int(round(s * gain)))) for s in pcm]


def decode_to_pcm(path: Path) -> list[int]:
    """用 ffmpeg 把任意格式转成 16 kHz 单声道 s16le。"""
    ffmpeg = os.environ.get("FFMPEG") or shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("ERROR: ffmpeg not found in PATH (needed to convert TTS audio)")
    result = subprocess.run(
        [ffmpeg, "-v", "error", "-i", str(path), "-ac", "1", "-ar", str(SAMPLE_RATE),
         "-f", "s16le", "-"],
        capture_output=True, check=True,
    )
    raw = result.stdout
    return list(struct.unpack(f"<{len(raw) // 2}h", raw[: len(raw) // 2 * 2]))


# --------------------------------------------------------------------------
# 在线合成（Microsoft Edge 朗读，edge-tts）
# --------------------------------------------------------------------------

def raw_name(text: str, voice: str, rate: str) -> str:
    """录音缓存文件名：由语音、语速、文本决定，与 clip 序号无关。"""
    digest = hashlib.sha256(f"{voice}\n{rate}\n{text}".encode("utf-8")).hexdigest()[:20]
    return f"{digest}.mp3"


def synth_request(text: str, voice: str, rate: str, out: Path) -> None:
    """合成一条，写成 mp3。先写临时文件再改名，中断不会留下半截录音被当成缓存。"""
    try:
        import asyncio
        import edge_tts
    except ImportError as error:
        raise SystemExit("ERROR: synth needs the edge-tts package: pip install edge-tts") from error
    partial = out.with_suffix(".part")
    asyncio.run(edge_tts.Communicate(text, voice, rate=rate).save(str(partial)))
    if not partial.exists() or partial.stat().st_size == 0:
        raise RuntimeError("empty audio returned")
    partial.replace(out)


def cmd_manifest(args: argparse.Namespace) -> int:
    content = kc.load()
    for clip_id, text in enumerate(kc.clip_table(content)):
        print(f"{clip_id}\t{text}")
    return 0


def cmd_synth(args: argparse.Namespace) -> int:
    raw_dir = Path(args.workdir) / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)

    texts = kc.clip_table(kc.load())
    todo = [(i, t) for i, t in enumerate(texts)
            if not (raw_dir / raw_name(t, args.voice, args.rate)).exists()]
    if args.limit:
        todo = todo[: args.limit]
    print(f"{len(texts)} clips, {len(texts) - len(todo)} already present, synthesizing {len(todo)} "
          f"with {args.voice} ({args.rate})")

    failed = []
    for n, (clip_id, text) in enumerate(todo, 1):
        out = raw_dir / raw_name(text, args.voice, args.rate)
        for attempt in range(3):   # 在线服务偶尔断连：同一条最多试三次
            try:
                synth_request(text, args.voice, args.rate, out)
                print(f"[{n}/{len(todo)}] {clip_id} {text} ({out.stat().st_size} bytes)")
                break
            except Exception as error:  # noqa: BLE001 - edge-tts 抛出的异常类型不固定
                if attempt == 2:
                    print(f"[{n}/{len(todo)}] {clip_id} {text} FAILED: {error}", file=sys.stderr)
                    failed.append(clip_id)
                else:
                    time.sleep(1.0 + attempt)
        time.sleep(args.delay)
    if failed:
        print(f"{len(failed)} clip(s) failed: {failed}; rerun synth to retry", file=sys.stderr)
        return 1
    return 0


def cmd_pack(args: argparse.Namespace) -> int:
    texts = kc.clip_table(kc.load())
    raw_dir = Path(args.workdir) / "raw"
    clips: list[tuple[bytes, int] | None] = []
    missing = []
    total_seconds = 0.0
    for clip_id, text in enumerate(texts):
        source = raw_dir / raw_name(text, args.voice, args.rate)
        if not source.exists():
            clips.append(None)
            missing.append((clip_id, text))
            continue
        pcm = normalize(trim_silence(decode_to_pcm(source)))
        if len(pcm) < 2:
            clips.append(None)
            missing.append((clip_id, text))
            continue
        clips.append((adpcm_encode(pcm), len(pcm)))
        total_seconds += len(pcm) / SAMPLE_RATE

    blob = build_pack(clips)
    parse_pack(blob)   # 写盘前自检
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print(f"wrote {out} ({len(blob)} bytes, {len(texts) - len(missing)}/{len(texts)} clips, "
          f"{total_seconds:.1f} s)")
    if missing:
        print(f"WARNING: {len(missing)} clip(s) missing and will play silently:", file=sys.stderr)
        for clip_id, text in missing[:20]:
            print(f"  {clip_id}\t{text}", file=sys.stderr)
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    blob = Path(args.pack).read_bytes()
    info = parse_pack(blob)
    present = [c for c in info["clips"] if c]
    seconds = sum(c["samples"] for c in present) / SAMPLE_RATE
    for clip in present:
        adpcm_decode(blob[clip["offset"]: clip["offset"] + clip["bytes"]], clip["samples"])
    print(f"kopack OK: {info['clip_count']} slots, {len(present)} present, "
          f"{seconds:.1f} s of audio, {info['total_size']} bytes")
    expected = len(kc.clip_table(kc.load()))
    if info["clip_count"] != expected:
        print(f"WARNING: pack has {info['clip_count']} slots but content has {expected} clips "
              "(pack is stale; rebuild it)", file=sys.stderr)
        return 1
    return 0


# --------------------------------------------------------------------------
# 主机测试夹具：小型合成发音包，字节内容由本文件的编码器产生，
# C 侧测试用固件解码器去解，两个独立实现互相印证。
# --------------------------------------------------------------------------

def fixture_clips() -> list[tuple[bytes, int] | None]:
    def tone(freq: float, samples: int, amplitude: float) -> list[int]:
        return [int(amplitude * math.sin(2 * math.pi * freq * i / SAMPLE_RATE))
                for i in range(samples)]

    sweep = [int(12000 * math.sin(2 * math.pi * (200 + 4 * i) * i / SAMPLE_RATE / 2))
             for i in range(301)]                       # 奇数个采样：覆盖末尾半字节
    loud = [(-1) ** i * 30000 for i in range(64)]        # 逼近满幅、方向来回翻转：覆盖钳位
    clips: list[tuple[bytes, int] | None] = [
        (adpcm_encode(tone(440, 400, 9000)), 400),
        None,                                            # 缺失项
        (adpcm_encode(sweep), len(sweep)),
        (adpcm_encode(loud), len(loud)),
    ]
    return clips


def render_fixture_header() -> str:
    clips = fixture_clips()
    blob = build_pack(clips)
    info = parse_pack(blob)
    lines = [
        "// GENERATED by `python3 tools/korean_audio.py fixture` — DO NOT EDIT.",
        "// 主机测试夹具：由 Python 编码器产生的小型 kopack，以及 Python 解码器给出的期望值。",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"#define KO_FIXTURE_CLIPS {len(clips)}",
        "static const uint8_t KO_FIXTURE_PACK[] = {",
    ]
    for i in range(0, len(blob), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in blob[i:i + 16]) + ",")
    lines += ["};", "",
              "// 每条发音：采样数（0 = 缺失）、Python 解码结果的 CRC-32（按 int16 小端字节）、前 4 个采样。",
              "typedef struct { uint32_t samples; uint32_t pcm_crc32; int16_t head[4]; } ko_fixture_expect_t;",
              "static const ko_fixture_expect_t KO_FIXTURE_EXPECT[KO_FIXTURE_CLIPS] = {"]
    for clip in info["clips"]:
        if clip is None:
            lines.append("    { 0, 0, { 0, 0, 0, 0 } },")
            continue
        pcm = adpcm_decode(blob[clip["offset"]: clip["offset"] + clip["bytes"]], clip["samples"])
        crc = zlib_crc32(struct.pack(f"<{len(pcm)}h", *pcm))
        head = (pcm + [0, 0, 0, 0])[:4]
        lines.append(f"    {{ {clip['samples']}, 0x{crc:08X}u, {{ {', '.join(map(str, head))} }} }},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def synthetic_clips() -> list[tuple[bytes, int]]:
    """每条"发音"是一段按 clip id 变化音高的提示音，用来验证整条音频链路，不是真发音。"""
    texts = kc.clip_table(kc.load())
    clips = []
    for clip_id, text in enumerate(texts):
        freq = 300.0 + (clip_id * 37) % 700           # 不同条目音高不同，耳朵能分辨播的是哪一条
        seconds = 1.4 if " " in text else 0.5           # 短语（含空格）比单词长
        samples = int(SAMPLE_RATE * seconds)
        ramp = SAMPLE_RATE // 50
        pcm = []
        for i in range(samples):
            gain = min(1.0, i / ramp, (samples - 1 - i) / ramp)
            pcm.append(int(9000 * gain * math.sin(2 * math.pi * freq * i / SAMPLE_RATE)))
        clips.append((adpcm_encode(pcm), len(pcm)))
    return clips


def cmd_synthetic(args: argparse.Namespace) -> int:
    blob = build_pack(synthetic_clips())
    parse_pack(blob)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print(f"wrote {out} ({len(blob)} bytes): SYNTHETIC TONES, not speech. "
          "It only exercises the playback path; build the real pack with `synth` + `pack`.")
    return 0


def cmd_fixture(args: argparse.Namespace) -> int:
    out = Path(args.out)
    text = render_fixture_header()
    if args.check:
        if not out.is_file() or out.read_text(encoding="utf-8") != text:
            print(f"ERROR: {out} is stale; run `python3 tools/korean_audio.py fixture`",
                  file=sys.stderr)
            return 1
        print("kopack fixture: PASS (up to date)")
        return 0
    out.write_text(text, encoding="utf-8")
    print(f"wrote {out}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("manifest", help="print clip_id<TAB>text for every clip")

    synth = sub.add_parser("synth", help="synthesize clips with Microsoft Edge TTS (edge-tts)")
    synth.add_argument("--workdir", default=str(DEFAULT_WORKDIR))
    synth.add_argument("--voice", default=DEFAULT_VOICE)
    synth.add_argument("--rate", default=DEFAULT_RATE)
    synth.add_argument("--limit", type=int, default=0, help="only synthesize the first N missing clips")
    synth.add_argument("--delay", type=float, default=0.3, help="seconds between requests")

    pack = sub.add_parser("pack", help="build kopack.bin from synthesized clips")
    pack.add_argument("--workdir", default=str(DEFAULT_WORKDIR))
    pack.add_argument("--voice", default=DEFAULT_VOICE)
    pack.add_argument("--rate", default=DEFAULT_RATE)
    pack.add_argument("--out", default=str(DEFAULT_PACK))

    verify = sub.add_parser("verify", help="validate a kopack.bin")
    verify.add_argument("pack", nargs="?", default=str(DEFAULT_PACK))

    synthetic = sub.add_parser("synthetic", help="write a tone-only pack that exercises the playback path")
    synthetic.add_argument("--out", default=str(DEFAULT_WORKDIR / "kopack-synthetic.bin"))

    fixture = sub.add_parser("fixture", help="write the host-test fixture header")
    fixture.add_argument("--out", default=str(ROOT / "tests" / "ko_pack_fixture.h"))
    fixture.add_argument("--check", action="store_true", help="fail if the header is stale")

    args = parser.parse_args(argv)
    return {"manifest": cmd_manifest, "synth": cmd_synth, "pack": cmd_pack,
            "verify": cmd_verify, "synthetic": cmd_synthetic, "fixture": cmd_fixture}[args.command](args)


if __name__ == "__main__":
    raise SystemExit(main())
