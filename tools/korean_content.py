#!/usr/bin/env python3
"""韩语学习应用的内容库：加载 / 校验 content.json，推导字体字符集与发音片段清单。

这是"显示什么"的唯一事实来源的读取层。生成器（gen_korean_assets.py）、发音工具
（korean_audio.py）和测试（tests/test_korean_assets.py）都通过它读内容，因此
字体字符集、C 数据表和发音清单不可能各说各话。

只依赖标准库，方便在 CI 里直接运行。
"""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT_PATH = ROOT / "assets" / "korean" / "content.json"

# 谚文音节、兼容字母（jamo）区间。
HANGUL_SYLLABLES = (0xAC00, 0xD7A3)
HANGUL_COMPAT_JAMO = (0x3131, 0x318E)
ASCII_PRINTABLE = (0x20, 0x7E)

# 界面里用来画按键提示、装饰的符号（需要字体里确实有字形）。
SYMBOLS = ""   # 符号统一写进 content.json 的 ui.sym_*，随中文字符集一起推导

# 必须有的字段。缺一个就在生成阶段报错，而不是等到板子上才发现。
LETTER_FIELDS = (
    "group", "ch", "name", "name_rom", "sound", "hint",
    "pair", "syl", "syl_rom", "ex", "ex_rom", "ex_zh",
)
ITEM_FIELDS = ("ko", "rom", "zh")

# 音标行（romanization / sound 等）只允许 ASCII，字体才能保证覆盖。
ASCII_RE = re.compile(r"^[\x20-\x7E]+$")

# 一轮学习最多 KO_SESSION_MAX 张；这里的 deck 不能小于测验需要的选项数。
MIN_DECK_SIZE = 4
QUIZ_OPTIONS = 4


class ContentError(ValueError):
    """content.json 不满足约定。"""


@dataclass(frozen=True)
class Content:
    raw: dict
    letters: list
    letter_groups: list
    topics: list
    phrase_groups: list
    ui: dict
    ui_ko: dict

    @property
    def words(self) -> list:
        return [w for topic in self.topics for w in topic["words"]]

    @property
    def phrases(self) -> list:
        return [p for group in self.phrase_groups for p in group["phrases"]]


def load(path: Path = CONTENT_PATH) -> Content:
    with path.open(encoding="utf-8") as handle:
        raw = json.load(handle)
    content = Content(
        raw=raw,
        letters=raw["letters"],
        letter_groups=raw["letter_groups"],
        topics=raw["topics"],
        phrase_groups=raw["phrase_groups"],
        ui=raw["ui"],
        ui_ko=raw["ui_ko"],
    )
    validate(content)
    return content


def codepoints(text: str) -> list[int]:
    return [ord(ch) for ch in text]


def is_hangul_syllable(cp: int) -> bool:
    return HANGUL_SYLLABLES[0] <= cp <= HANGUL_SYLLABLES[1]


def is_compat_jamo(cp: int) -> bool:
    return HANGUL_COMPAT_JAMO[0] <= cp <= HANGUL_COMPAT_JAMO[1]


def is_ascii_printable(cp: int) -> bool:
    return ASCII_PRINTABLE[0] <= cp <= ASCII_PRINTABLE[1]


def validate(content: Content) -> None:
    """尽早、明确地拒绝坏内容；每条消息都指出是哪一项。"""
    errors: list[str] = []

    group_ids = [g["id"] for g in content.letter_groups]
    if len(set(group_ids)) != len(group_ids):
        errors.append("letter_groups: duplicate id")

    seen_group_order: list[str] = []
    for index, letter in enumerate(content.letters):
        where = f"letters[{index}]({letter.get('ch', '?')})"
        for field in LETTER_FIELDS:
            if not isinstance(letter.get(field), str) or not letter[field]:
                errors.append(f"{where}: missing field {field}")
        if letter.get("group") not in group_ids:
            errors.append(f"{where}: unknown group {letter.get('group')!r}")
        for field in ("name_rom", "sound", "syl_rom", "ex_rom"):
            if isinstance(letter.get(field), str) and not ASCII_RE.match(letter[field]):
                errors.append(f"{where}.{field}: must be printable ASCII")
        ch = letter.get("ch", "")
        if len(ch) != 1 or not is_compat_jamo(ord(ch)):
            errors.append(f"{where}: ch must be one Hangul compatibility jamo")
        if not seen_group_order or seen_group_order[-1] != letter.get("group"):
            seen_group_order.append(letter.get("group"))
    if len(seen_group_order) != len(set(seen_group_order)):
        errors.append("letters: each group must be one contiguous run")
    if seen_group_order != [g for g in group_ids if g in seen_group_order]:
        errors.append("letters: group runs must follow letter_groups order")
    if len({letter.get("ch") for letter in content.letters}) != len(content.letters):
        errors.append("letters: duplicate ch")

    def check_items(kind: str, decks: list, key: str) -> None:
        for deck in decks:
            items = deck[key]
            if len(items) < MIN_DECK_SIZE:
                errors.append(f"{kind} {deck['id']}: needs at least {MIN_DECK_SIZE} items")
            for item in items:
                where = f"{kind} {deck['id']}/{item.get('ko', '?')}"
                for field in ITEM_FIELDS:
                    if not isinstance(item.get(field), str) or not item[field]:
                        errors.append(f"{where}: missing field {field}")
                if isinstance(item.get("rom"), str) and not ASCII_RE.match(item["rom"]):
                    errors.append(f"{where}.rom: must be printable ASCII")

    check_items("topic", content.topics, "words")
    check_items("phrase_group", content.phrase_groups, "phrases")

    # 同一 deck 内韩文/中文不能重复，否则测验里两个选项会一样，无法作答。
    for deck, key in [(t, "words") for t in content.topics] + [
        (g, "phrases") for g in content.phrase_groups
    ]:
        for field in ("ko", "zh"):
            values = [item[field] for item in deck[key] if field in item]
            if len(set(values)) != len(values):
                errors.append(f"{deck['id']}: duplicate {field} inside one deck")
    # 全部词汇的韩文必须唯一：测验的"看中文选韩文"和"听音选词"按韩文去重选项。
    words_ko = [w["ko"] for w in content.words]
    if len(set(words_ko)) != len(words_ko):
        errors.append("topics: duplicate Korean word across topics (homonyms are ambiguous in quizzes)")

    for section, table in (("ui", content.ui), ("ui_ko", content.ui_ko)):
        for key, value in table.items():
            if not re.fullmatch(r"[a-z0-9_]+", key):
                errors.append(f"{section}.{key}: key must be lowercase snake_case")
            if not isinstance(value, str) or not value:
                errors.append(f"{section}.{key}: empty value")

    if errors:
        raise ContentError("content.json is invalid:\n  " + "\n  ".join(errors))


# --------------------------------------------------------------------------
# 字符集推导
# --------------------------------------------------------------------------

def _chars(strings) -> set[int]:
    result: set[int] = set()
    for text in strings:
        result.update(codepoints(text))
    return result


def zh_strings(content: Content) -> list[str]:
    """所有用中文字体显示的文字（可含 ASCII 数字、格式符）。"""
    strings = list(content.ui.values())
    strings += [g["zh"] for g in content.letter_groups]
    for letter in content.letters:
        strings += [letter["hint"], letter["ex_zh"]]
    for topic in content.topics:
        strings.append(topic["zh"])
        strings += [w["zh"] for w in topic["words"]]
    for group in content.phrase_groups:
        strings.append(group["zh"])
        strings += [p["zh"] for p in group["phrases"]]
    return strings


def ko_strings_all(content: Content) -> list[str]:
    """所有用韩文字体显示的文字（词汇、短语、字母名、例词、装饰字）。"""
    strings = list(content.ui_ko.values())
    strings += [g["ko"] for g in content.letter_groups]
    for letter in content.letters:
        strings += [letter["name"], letter["syl"], letter["ex"], letter["pair"], letter["ch"]]
    strings += [w["ko"] for w in content.words]
    strings += [p["ko"] for p in content.phrases]
    return strings


def ko_strings_phrases_and_words(content: Content) -> list[str]:
    return [w["ko"] for w in content.words] + [p["ko"] for p in content.phrases]


def ko_strings_words(content: Content) -> list[str]:
    return [w["ko"] for w in content.words]


def ascii_all() -> set[int]:
    return set(range(ASCII_PRINTABLE[0], ASCII_PRINTABLE[1] + 1))


def symbol_set() -> set[int]:
    return _chars([SYMBOLS])


def charset(content: Content, font: str) -> dict[str, list[int]]:
    """返回该字体需要的码点，按来源字体分组：{"sc": [...], "kr": [...]}。

    字体名 -> 谁用它：
      ko_zh14 / ko_zh20  中文界面与释义（拉丁字母取自 SC）
      ko_ko24            所有韩文（含字母名、拼读行、装饰字）
      ko_ko32            词汇 + 短语（较长词与短语换行显示）
      ko_ko48            词汇单词（闪卡主体）、字母测验的题干字母
      ko_jamo96          字母卡片上的大字母
    """
    if font in ("ko_zh14", "ko_zh20"):
        zh = {cp for cp in _chars(zh_strings(content)) if not is_ascii_printable(cp)}
        zh |= symbol_set()
        return {"sc": sorted(zh | ascii_all()), "kr": []}
    if font == "ko_ko24":
        ko = _chars(ko_strings_all(content))
        return {"sc": [], "kr": sorted(ko | ascii_all())}
    if font == "ko_ko32":
        ko = _chars(ko_strings_phrases_and_words(content))
        return {"sc": [], "kr": sorted(ko | ascii_all())}
    if font == "ko_ko48":
        ko = _chars(ko_strings_words(content)) | _chars(l["ch"] for l in content.letters)
        return {"sc": [], "kr": sorted(ko | ascii_all())}
    if font == "ko_jamo96":
        return {"sc": [], "kr": sorted(_chars(l["ch"] for l in content.letters))}
    raise KeyError(font)


def charset_digest(points: dict[str, list[int]]) -> str:
    blob = json.dumps(points, sort_keys=True).encode()
    return hashlib.sha256(blob).hexdigest()


def to_ranges(points: list[int]) -> str:
    """把码点排序后合并成 lv_font_conv 的 --range 参数（0x20-0x7E,0xAC00,...）。"""
    ranges: list[tuple[int, int]] = []
    for cp in sorted(set(points)):
        if ranges and cp == ranges[-1][1] + 1:
            ranges[-1] = (ranges[-1][0], cp)
        else:
            ranges.append((cp, cp))
    return ",".join(
        f"0x{lo:X}" if lo == hi else f"0x{lo:X}-0x{hi:X}" for lo, hi in ranges
    )


# --------------------------------------------------------------------------
# 发音片段：同样的韩文文本只生成一次，条目引用同一 clip id。
# --------------------------------------------------------------------------

def clip_table(content: Content) -> list[str]:
    """按固定顺序去重，返回 clip id -> 文本。顺序变了 id 就变，所以是内容契约的一部分。"""
    texts: list[str] = []
    seen: set[str] = set()

    def add(text: str) -> None:
        if text not in seen:
            seen.add(text)
            texts.append(text)

    for letter in content.letters:
        add(letter["name"])
    for letter in content.letters:
        add(letter["ex"])
    for word in content.words:
        add(word["ko"])
    for phrase in content.phrases:
        add(phrase["ko"])
    return texts


def clip_id(texts: list[str], text: str) -> int:
    return texts.index(text)


# --------------------------------------------------------------------------
# C 源码里的 UI 字符串引用检查：应用代码不允许直接写中文/韩文字面量。
# --------------------------------------------------------------------------

NON_ASCII_LITERAL_RE = re.compile(r'"(?:[^"\\\n]|\\.)*[^\x00-\x7F](?:[^"\\\n]|\\.)*"')


LOG_STATEMENT_RE = re.compile(r"\bESP_LOG[EWIDV]\s*\(.*?\)\s*;", re.S)


def find_non_ascii_literals(source: str) -> list[str]:
    """返回 C 源码中含非 ASCII 字符的字符串字面量。

    忽略注释，也忽略 ESP_LOG* 语句：日志走串口、不显示在屏幕上，不需要字体；
    这条规则要防的是"界面文字绕过字符串表，导致字体缺字"。
    """
    without_block = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    without_line = re.sub(r"//[^\n]*", "", without_block)
    without_logs = LOG_STATEMENT_RE.sub("", without_line)
    return NON_ASCII_LITERAL_RE.findall(without_logs)


def main() -> int:
    content = load()
    texts = clip_table(content)
    print(f"letters={len(content.letters)} words={len(content.words)} "
          f"phrases={len(content.phrases)} topics={len(content.topics)} "
          f"phrase_groups={len(content.phrase_groups)} clips={len(texts)}")
    for font in ("ko_zh14", "ko_zh20", "ko_ko24", "ko_ko32", "ko_ko48", "ko_jamo96"):
        points = charset(content, font)
        print(f"{font}: sc={len(points['sc'])} kr={len(points['kr'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
