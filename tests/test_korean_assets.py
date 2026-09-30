#!/usr/bin/env python3
"""Host tests for the Korean learner's generated assets and audio tooling.

Covers what a successful firmware build cannot prove: every displayed code point
has a glyph in the font that draws it, generated sources are not stale, no
hard-coded CJK/Hangul literals bypass the string table, and the audio tool's
ADPCM/pack format agrees with the firmware.
"""

from __future__ import annotations

import importlib.util
import math
import re
import struct
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import korean_content as kc  # noqa: E402


def load_tool(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


GEN = load_tool("gen_korean_assets")
AUDIO = load_tool("korean_audio")

MAIN = ROOT / "main"
GENERATED_SOURCES = {"ko_content_gen.c", "ko_content_gen.h", "ko_strings.h"}


def font_points(name: str) -> set[int]:
    return GEN.parse_font_codepoints(GEN.FONT_DIR / f"{name}.c")


class ContentTests(unittest.TestCase):
    def test_content_is_valid(self) -> None:
        content = kc.load()
        self.assertEqual(len(content.letters), 40)
        self.assertGreaterEqual(len(content.words), 100)
        self.assertGreaterEqual(len(content.phrases), 20)

    def test_validation_rejects_bad_content(self) -> None:
        good = kc.load()

        def broken(mutate) -> kc.Content:
            raw = __import__("copy").deepcopy(good.raw)
            mutate(raw)
            return kc.Content(raw, raw["letters"], raw["letter_groups"], raw["topics"],
                              raw["phrase_groups"], raw["ui"], raw["ui_ko"])

        cases = {
            "duplicate zh in a deck": lambda r: r["topics"][0]["words"][1].update(
                zh=r["topics"][0]["words"][0]["zh"]),
            "non-ASCII romanization": lambda r: r["topics"][0]["words"][0].update(rom="안녕"),
            "missing letter field": lambda r: r["letters"][0].pop("hint"),
            "deck too small for a quiz": lambda r: r["topics"][0].update(
                words=r["topics"][0]["words"][:2]),
            "duplicate word across topics": lambda r: r["topics"][1]["words"][0].update(
                ko=r["topics"][0]["words"][0]["ko"]),
            "bad ui key": lambda r: r["ui"].update({"Bad-Key": "x"}),
            "letter is not a jamo": lambda r: r["letters"][0].update(ch="A"),
        }
        for label, mutate in cases.items():
            with self.subTest(label), self.assertRaises(kc.ContentError):
                kc.validate(broken(mutate))

    def test_clip_table_is_deduplicated_and_stable(self) -> None:
        content = kc.load()
        texts = kc.clip_table(content)
        self.assertEqual(len(texts), len(set(texts)))
        # Every audible string maps to a clip.
        for word in content.words:
            self.assertIn(word["ko"], texts)
        for phrase in content.phrases:
            self.assertIn(phrase["ko"], texts)
        for letter in content.letters:
            self.assertIn(letter["name"], texts)
            self.assertIn(letter["ex"], texts)


class GeneratedSourceTests(unittest.TestCase):
    def test_generated_files_match_content(self) -> None:
        self.assertEqual(GEN.run_check(kc.load()), [])

    def test_check_detects_a_missing_glyph(self) -> None:
        """The coverage check must be able to fail: add a code point no font has."""
        real = kc.charset

        def with_extra(content, font):
            points = real(content, font)
            if font == "ko_ko24":
                points = {**points, "kr": sorted(set(points["kr"]) | {0xD7A3})}
            return points

        with mock.patch.object(kc, "charset", with_extra):
            errors = GEN.run_check(kc.load())
        self.assertTrue(any("ko_ko24" in error for error in errors), errors)

    def test_check_detects_stale_generated_source(self) -> None:
        content = kc.load()
        mutated = kc.Content(content.raw, content.letters, content.letter_groups, content.topics,
                             content.phrase_groups, {**content.ui, "app_title": "别的标题"},
                             content.ui_ko)
        errors = GEN.run_check(mutated)
        self.assertTrue(any("ko_strings.h" in error and "stale" in error for error in errors), errors)

    def test_generated_fonts_have_no_absolute_paths(self) -> None:
        for path in GEN.FONT_DIR.glob("ko_*.c"):
            head = path.read_text(encoding="utf-8")[:20000]
            self.assertIsNone(re.search(r"(/Users/|/home/|[A-Za-z]:\\\\)", head), path.name)

    def test_manifest_records_provenance(self) -> None:
        import json
        manifest = json.loads(GEN.MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(manifest["converter"]["version"], GEN.CONVERTER_VERSION)
        for key, meta in manifest["sources"].items():
            self.assertRegex(meta["sha256"], r"^[0-9a-f]{64}$", key)
            self.assertIn("SIL Open Font License", meta["license"])
        for name, font in manifest["fonts"].items():
            self.assertIn("--no-compress", font["command"], name)
            self.assertNotIn("/Users/", " ".join(font["command"]), name)   # no machine-specific paths


class FontCoverageTests(unittest.TestCase):
    """Each string must be covered by the font size that actually draws it."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.content = kc.load()
        cls.fonts = {name: font_points(name) for name in GEN.FONT_SPECS}

    def assert_covered(self, texts, font: str, allow=frozenset()) -> None:
        missing = {ord(c) for text in texts for c in text} - self.fonts[font] - set(allow)
        self.assertFalse(missing, f"{font} lacks {[f'U+{c:04X}' for c in sorted(missing)]}")

    def test_chinese_ui_and_meanings(self) -> None:
        for font in ("ko_zh14", "ko_zh20"):
            self.assert_covered(kc.zh_strings(self.content), font)

    def test_ui_symbols_present(self) -> None:
        symbols = [v for k, v in self.content.ui.items() if k.startswith("sym_")]
        self.assertGreaterEqual(len(symbols), 6)
        for font in ("ko_zh14", "ko_zh20"):
            self.assert_covered(symbols, font)

    def test_korean_text_by_role(self) -> None:
        c = self.content
        self.assert_covered(kc.ko_strings_all(c), "ko_ko24")
        self.assert_covered(kc.ko_strings_phrases_and_words(c), "ko_ko32")
        self.assert_covered(kc.ko_strings_words(c), "ko_ko48")
        self.assert_covered([l["ch"] for l in c.letters], "ko_ko48")   # 字母测验的题干
        self.assert_covered([l["ch"] for l in c.letters], "ko_jamo96")

    def test_romanization_is_ascii_in_every_text_font(self) -> None:
        c = self.content
        roms = [w["rom"] for w in c.words] + [p["rom"] for p in c.phrases]
        roms += [l[k] for l in c.letters for k in ("name_rom", "sound", "syl_rom", "ex_rom")]
        for font in ("ko_zh14", "ko_zh20", "ko_ko24", "ko_ko32"):
            self.assert_covered(roms, font)

    def test_placeholder_glyph_is_not_counted_as_coverage(self) -> None:
        # A code point deliberately outside every charset must be reported missing.
        self.assertNotIn(0x9F98, self.fonts["ko_zh20"])
        self.assertNotIn(0xD7A3, self.fonts["ko_ko24"])

    def test_no_font_is_wastefully_large(self) -> None:
        # Guard against accidentally embedding a whole CJK block.
        for name, points in self.fonts.items():
            self.assertLess(len(points), 1500, name)


class SourceCodeRuleTests(unittest.TestCase):
    """Application code must take every displayed string from the generated table."""

    def app_sources(self) -> list[Path]:
        return sorted(p for p in MAIN.glob("ko_*.[ch]") if p.name not in GENERATED_SOURCES)

    def test_no_hardcoded_cjk_or_hangul_literals(self) -> None:
        offenders = {}
        for path in self.app_sources():
            found = kc.find_non_ascii_literals(path.read_text(encoding="utf-8"))
            if found:
                offenders[path.name] = found
        self.assertEqual(offenders, {})

    def test_string_macros_are_defined_and_used(self) -> None:
        header = (MAIN / "ko_strings.h").read_text(encoding="utf-8")
        defined = set(re.findall(r"#define (KO_[SK]_[A-Z0-9_]+)", header))
        used: set[str] = set()
        for path in self.app_sources():
            used |= set(re.findall(r"\bKO_[SK]_[A-Z0-9_]+\b", path.read_text(encoding="utf-8")))
        self.assertEqual(sorted(used - defined), [], "used but not defined")
        # An unused string still costs its glyphs in Flash: keep the table honest.
        self.assertEqual(sorted(defined - used), [], "defined but never used")

    def test_non_ascii_literal_detector_works(self) -> None:
        self.assertEqual(kc.find_non_ascii_literals('const char *a = "hello";'), [])
        self.assertEqual(len(kc.find_non_ascii_literals('x = "韩语";')), 1)
        self.assertEqual(kc.find_non_ascii_literals('// "韩语" in a comment\n/* "가" */'), [])
        # Log statements never reach the screen, so they are exempt; a label is not.
        self.assertEqual(kc.find_non_ascii_literals('ESP_LOGI(TAG, "就绪 %d",\n         x);'), [])
        self.assertEqual(len(kc.find_non_ascii_literals('ESP_LOGI(TAG, "ok"); lv_label_set_text(l, "就绪");')), 1)


class AdpcmTests(unittest.TestCase):
    def sine(self, freq: float, samples: int, amplitude: float) -> list[int]:
        return [int(amplitude * math.sin(2 * math.pi * freq * i / AUDIO.SAMPLE_RATE))
                for i in range(samples)]

    def test_roundtrip_quality(self) -> None:
        pcm = self.sine(440, 1600, 10000)
        decoded = AUDIO.adpcm_decode(AUDIO.adpcm_encode(pcm), len(pcm))
        self.assertEqual(len(decoded), len(pcm))
        noise = sum((a - b) ** 2 for a, b in zip(pcm, decoded))
        signal = sum(a * a for a in pcm)
        snr_db = 10 * math.log10(signal / max(noise, 1))
        self.assertGreater(snr_db, 20.0)   # 4-bit ADPCM comfortably clears 20 dB on a tone

    def test_odd_and_tiny_lengths(self) -> None:
        for n in (1, 2, 3, 4, 5, 64, 65):
            pcm = self.sine(300, n, 8000)
            decoded = AUDIO.adpcm_decode(AUDIO.adpcm_encode(pcm), n)
            self.assertEqual(len(decoded), n)
            self.assertEqual(decoded[0], pcm[0])   # the header carries sample 0 exactly

    def test_extremes_do_not_overflow(self) -> None:
        pcm = [(-1) ** i * 32767 for i in range(200)] + [-32768] * 50
        decoded = AUDIO.adpcm_decode(AUDIO.adpcm_encode(pcm), len(pcm))
        self.assertTrue(all(-32768 <= v <= 32767 for v in decoded))

    def test_matches_ffmpeg_reference(self) -> None:
        """Independent implementation check: ffmpeg's adpcm_ima_wav decodes our stream."""
        ffmpeg = subprocess.run(["which", "ffmpeg"], capture_output=True, text=True).stdout.strip()
        if not ffmpeg:
            self.skipTest("ffmpeg not installed")
        import tempfile
        pcm = self.sine(500, 505, 9000)   # one 256-byte ima_wav block holds 505 samples
        stream = AUDIO.adpcm_encode(pcm)
        # ima_wav mono block = header (predictor s16, index u8, reserved) + nibbles,
        # the same layout as our stream; wrap it in a minimal WAV container.
        block_align = len(stream)
        fmt = struct.pack("<HHIIHHHHH", 0x11, 1, AUDIO.SAMPLE_RATE, AUDIO.SAMPLE_RATE * block_align // 505,
                          block_align, 4, 2, 505, 0)
        wav = (b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(stream)) + b"WAVE"
               + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(stream))
               + stream)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "block.wav"
            src.write_bytes(wav)
            result = subprocess.run([ffmpeg, "-v", "error", "-i", str(src), "-f", "s16le", "-"],
                                    capture_output=True)
        if result.returncode != 0 or not result.stdout:
            self.skipTest("this ffmpeg build cannot decode ima_wav")
        theirs = list(struct.unpack(f"<{len(result.stdout) // 2}h", result.stdout))
        ours = AUDIO.adpcm_decode(stream, 505)
        self.assertEqual(theirs[: len(ours)], ours)


class PackTests(unittest.TestCase):
    def test_build_and_parse(self) -> None:
        clips = AUDIO.fixture_clips()
        blob = AUDIO.build_pack(clips)
        info = AUDIO.parse_pack(blob)
        self.assertEqual(info["clip_count"], len(clips))
        self.assertIsNone(info["clips"][1])
        self.assertEqual(len(blob) % 4, 0)
        for clip in info["clips"]:
            if clip:
                self.assertEqual(clip["offset"] % 4, 0)

    def test_corruption_is_detected(self) -> None:
        blob = bytearray(AUDIO.build_pack(AUDIO.fixture_clips()))
        for index in list(range(AUDIO.HEADER_BYTES)) + list(range(AUDIO.HEADER_BYTES, AUDIO.HEADER_BYTES + 12 * 4)):
            corrupt = bytearray(blob)
            corrupt[index] ^= 0x01
            with self.subTest(byte=index), self.assertRaises(ValueError):
                AUDIO.parse_pack(bytes(corrupt))

    def test_fixture_header_is_current(self) -> None:
        self.assertEqual(AUDIO.main(["fixture", "--check"]), 0)

    def test_full_pack_fits_the_partition(self) -> None:
        """Budget check: worst-case audio for the real content must fit kopack."""
        texts = kc.clip_table(kc.load())
        # 16 kHz ADPCM = 8000 B/s. Assume 1.6 s per word, 3.2 s per phrase (generous).
        words = sum(1 for t in texts if " " not in t)
        seconds = words * 1.6 + (len(texts) - words) * 3.2
        estimate = int(seconds * AUDIO.SAMPLE_RATE / 2) + len(texts) * (AUDIO.ENTRY_BYTES + 8)
        partition = self.kopack_partition_size()
        self.assertLess(estimate, partition)

    @staticmethod
    def kopack_partition_size() -> int:
        for line in (ROOT / "partitions.csv").read_text(encoding="utf-8").splitlines():
            cells = [c.strip() for c in line.split(",")]
            if cells and cells[0] == "kopack":
                return int(cells[4], 0)
        raise AssertionError("partitions.csv has no kopack partition")


class SignalProcessingTests(unittest.TestCase):
    def test_trim_silence(self) -> None:
        pcm = [0] * 4000 + [8000, -8000] * 500 + [0] * 4000
        trimmed = AUDIO.trim_silence(pcm)
        self.assertLess(len(trimmed), len(pcm))
        self.assertGreaterEqual(len(trimmed), 1000)          # voiced part is intact
        self.assertLess(len(trimmed), 1000 + 16000 * 0.2)     # only a short margin is kept
        self.assertEqual(AUDIO.trim_silence([0] * 100), [0] * 100)   # all silence: unchanged

    def test_normalize_levels_and_limits(self) -> None:
        quiet = [200, -200] * 500
        loud = [20000, -20000] * 500
        rms = lambda p: math.sqrt(sum(s * s for s in p) / len(p))
        self.assertAlmostEqual(rms(AUDIO.normalize(quiet)), rms(AUDIO.normalize(loud)), delta=400)
        spiky = [30000] + [100] * 999                        # a peak must not clip after gain
        self.assertLessEqual(max(abs(s) for s in AUDIO.normalize(spiky)), 32767)
        self.assertEqual(AUDIO.normalize([0] * 10), [0] * 10)
        self.assertEqual(AUDIO.normalize([]), [])

    def test_raw_cache_is_keyed_by_text_not_clip_id(self) -> None:
        a = AUDIO.raw_name("안녕하세요", "ko-KR-SunHiNeural", "-10%")
        self.assertEqual(a, AUDIO.raw_name("안녕하세요", "ko-KR-SunHiNeural", "-10%"))
        self.assertNotEqual(a, AUDIO.raw_name("감사합니다", "ko-KR-SunHiNeural", "-10%"))
        self.assertNotEqual(a, AUDIO.raw_name("안녕하세요", "ko-KR-InJoonNeural", "-10%"))
        self.assertNotEqual(a, AUDIO.raw_name("안녕하세요", "ko-KR-SunHiNeural", "+0%"))
        self.assertTrue(a.endswith(".mp3"))

    def test_no_secret_in_audio_tool(self) -> None:
        source = (ROOT / "tools" / "korean_audio.py").read_text(encoding="utf-8")
        self.assertNotRegex(source, r"sk-[A-Za-z0-9]{16,}")
        self.assertNotIn("--api-key", source)

if __name__ == "__main__":
    unittest.main()
