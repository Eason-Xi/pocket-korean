<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Korean Learner for FoloToy AI Passport

An offline Korean study companion for Chinese-speaking beginners, built on the
AI Passport BSP. The whole interface is in Simplified Chinese; Korean is shown
with its Revised Romanization and, when the pronunciation pack is installed,
spoken aloud by the device speaker.

This `feature/korean-learner` branch replaces the baseline hardware-test menu
with its own redesigned screens. The upstream product overview remains in
[`docs/README.md`](docs/README.md).

## What it teaches

| Module | Content | How it works |
| --- | --- | --- |
| Hangul alphabet | 40 letters in 4 groups (basic vowels, basic consonants, tense consonants, compound vowels) | One letter per page: large glyph, letter name, sound, a Chinese pronunciation hint, a syllable-building line such as `ㄱ + ㅏ = 가`, and an example word |
| Vocabulary cards | 167 beginner words in 13 topics | Front shows Korean and romanization; flip to see the meaning, then mark it remembered or not. Rounds of up to 10 cards favour new and weak cards; a missed card returns three cards later |
| Everyday phrases | 28 sentences in 5 situations (meeting, restaurant, shopping, travel, asking for help) | Same card flow as vocabulary |
| Quiz | Korean to Chinese, Chinese to Korean, letter to sound, listen and choose | 10 questions per round, 4 options from the same topic, weighted towards unfamiliar items, with right/wrong tones |
| Settings | Volume, brightness, clear progress, pronunciation-pack and storage status | Clearing progress needs a second confirmation |

Progress (per-item familiarity, quiz totals) and settings are stored in NVS and
survive power loss. A card counts as mastered after being remembered or answered
correctly three times in a row; one miss resets it.

## Controls

| Key | Lists | Letter page | Card (front) | Card (back) | Quiz |
| --- | --- | --- | --- | --- | --- |
| UP | Move up | Previous letter | Play pronunciation | Not remembered | Previous option |
| OK | Open / change | Play name and example | Flip | Replay | Confirm / next |
| DOWN | Move down | Next letter | - | Remembered | Next option |
| Hold OK | Back | Back | Back | Back | Back |

UP and DOWN react on key press, so rapid presses are never merged into a
double-click. The footer always shows what the three keys do on the current
page. The top-right corner shows the battery level (`--` when the fuel gauge is
unavailable).

The screen dims after 20 s without input and turns off after 60 s. The first key
press only wakes the screen; it does not also act on the page.

## Build and flash

Requirements: ESP-IDF 5.5.3 (see
[`docs/development/engineering/environment-setup.md`](docs/development/engineering/environment-setup.md)).

```bash
./tools/validate.sh            # host tests, repository checks and firmware build
```

The verified merged image is `build/FoloToy-AI-Passport-full.bin`; flash it from
offset `0x0`:

```bash
python -m esptool --chip esp32c3 -p <port> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

Flashing the merged image resets NVS, so existing study progress on the device
is cleared.

## Pronunciation pack

Speech is not stored in Git. `tools/korean_audio.py` synthesizes every word,
phrase, letter name and example with Microsoft Edge online text-to-speech
(`edge-tts`, voice `ko-KR-SunHiNeural`, rate -10%), trims silence, normalizes
loudness, encodes 16 kHz 4-bit IMA-ADPCM, and writes
`assets/audio/generated/kopack.bin`:

```bash
pip install edge-tts           # a virtual environment is recommended
python3 tools/korean_audio.py synth    # about 256 clips, cached by text
python3 tools/korean_audio.py pack     # needs ffmpeg
python3 tools/korean_audio.py verify
./tools/validate.sh --firmware
```

When `kopack.bin` exists, the build flashes it to the `kopack` partition and
includes it in the merged image. Without it the firmware still builds and runs
silently: speaker icons are hidden, the listening quiz is greyed out, and Settings
reports "not installed". The generated audio comes from a third-party online service;
check its terms before redistributing it. `assets/audio/generated/` is ignored
by Git for that reason.

## Partition layout

| Partition | Offset | Size | Purpose |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | `0x6000` | Progress and settings |
| `phy_init` | `0xF000` | `0x1000` | PHY data |
| `factory` | `0x10000` | `0x2F0000` | Application (about 1.2 MB used) |
| `kopack` | `0x300000` | `0x500000` | Pronunciation pack (data, subtype `0x40`) |

## Changing the content

All displayed text comes from [`assets/korean/content.json`](assets/korean/content.json).
Append new items at the end of a list so saved progress stays aligned. Then
regenerate the tables, strings and font subsets:

```bash
python3 tools/gen_korean_assets.py generate --fonts \
  --lv-font-conv <path-to-lv_font_conv-1.5.3> \
  --font-sc <SourceHanSansSC-Regular.otf> --font-kr <SourceHanSansKR-Regular.otf>
```

The static gate fails if generated files are stale, if any displayed code point
is missing from the font that draws it, or if application code hard-codes Chinese
or Korean text instead of using `main/ko_strings.h`. Font sources and licenses
are listed in [`assets/README.md`](assets/README.md).

## Design notes

- Board access goes only through the BSP (`bsp_display_*`, `bsp_lvgl_*`,
  `bsp_button_*`, `bsp_audio_*`, `bsp_battery_*`). The baseline `demo_*.c` and
  `ui_pixel*` files remain in `main/` for their host tests but are not linked.
- Pure logic (`main/ko_*.c` except `ko_app`, `ko_audio`, `ko_store` and `ko_ui*`)
  is independent of ESP-IDF and covered by host tests; `ko_view.c` builds every
  screen model and is shared by the firmware, the host preview and the tests.
- Button callbacks only enqueue events. An input task updates state and draws
  under `bsp_lvgl_lock()`; an audio task owns all PCM writes and never touches
  LVGL; a status task handles dimming, the battery and saving.
- NVS writes wait until 1.5 s after the last change and never run while audio is
  playing, because Flash writes can starve I2S DMA.
- The LVGL pool is 32 KB (baseline: 24 KB) and the default theme is disabled.
  `tools/render_korean_preview.py` renders every screen on the host with the real
  LVGL and the application's UI code; the measured peak is about 22.9 KB, and the
  script fails above 75 % of the pool.

## Known limits

- The screen timeout only dims and switches off the backlight; the BSP has no
  key-wake sleep API, so the CPU keeps running while the screen is off.
- Chinese pronunciation hints are approximations for beginners.
- Romanization follows the Revised Romanization of Korean and describes
  pronunciation, so it does not show tensification.
