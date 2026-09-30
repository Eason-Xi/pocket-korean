<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# FoloToy AI Passport 韩语学习

面向中文母语初学者的离线韩语学习工具，基于 AI Passport 的 BSP 开发。界面全部是简体中文；
韩文同时显示修订罗马字，安装发音包后还能由设备喇叭读出来。

本 `feature/korean-learner` 分支用自己重新设计的界面替换了基线的硬件测试菜单。上游的产品总览仍在
[`docs/README.zh_CN.md`](docs/README.zh_CN.md)。

## 学习内容

| 模块 | 内容 | 使用方式 |
| --- | --- | --- |
| 韩文字母 | 40 个字母，分 4 组（基本元音、基本辅音、双辅音、复合元音） | 一页一个字母：大字形、字母名、读音、近似汉语读音提示、拼读式（如 `ㄱ + ㅏ = 가`）和例词 |
| 词汇卡片 | 13 个主题共 167 个初级词汇 | 正面显示韩文和罗马音，翻面看中文释义，再标记"记住了 / 没记住"。每轮最多 10 张，优先安排新卡和不熟的卡；没记住的卡隔 3 张后再出现 |
| 日常短语 | 5 个场景共 28 句（初次见面、餐厅点餐、购物、出行问路、求助沟通） | 与词汇卡片相同的流程 |
| 小测验 | 看韩文选中文、看中文选韩文、字母认读、听音选词 | 每轮 10 题，4 个选项来自同一主题，偏向不熟的条目，答对 / 答错有提示音 |
| 设置 | 音量、亮度、清除学习进度、发音包与进度存储状态 | 清除进度需要再按一次确认 |

学习进度（每个条目的熟练度、测验累计）和设置保存在 NVS 中，断电不丢。连续记住或答对 3 次算"已掌握"，
错一次就回到从头。

## 按键

| 按键 | 列表页 | 字母页 | 卡片正面 | 卡片背面 | 测验 |
| --- | --- | --- | --- | --- | --- |
| 上 | 上移 | 上一个字母 | 播放发音 | 没记住 | 上一个选项 |
| 确定 | 进入 / 调节 | 读字母名和例词 | 翻面 | 重播 | 确认 / 下一题 |
| 下 | 下移 | 下一个字母 | - | 记住了 | 下一个选项 |
| 长按确定 | 返回 | 返回 | 返回 | 返回 | 返回 |

上 / 下键在按下瞬间响应，连续快按不会被合并成双击。页脚始终显示当前页三个键的作用，右上角显示电量
（电量计不可用时显示 `--`）。

20 秒无操作屏幕调暗，60 秒后熄屏。熄屏时按的第一下只负责点亮屏幕，不会同时触发页面动作。

## 构建与烧录

需要 ESP-IDF 5.5.3（见
[`docs/development/engineering/environment-setup.zh_CN.md`](docs/development/engineering/environment-setup.zh_CN.md)）。

```bash
./tools/validate.sh            # 主机测试、仓库检查与固件构建
```

验证过的合并固件是 `build/FoloToy-AI-Passport-full.bin`，从 `0x0` 烧录：

```bash
python -m esptool --chip esp32c3 -p <port> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
```

烧录合并固件会重置 NVS，设备上已有的学习进度会被清空。

## 发音包

语音不存进 Git。`tools/korean_audio.py` 用 Microsoft Edge 在线朗读（`edge-tts`，语音
`ko-KR-SunHiNeural`，语速 -10%）合成所有词汇、短语、字母名和例词，去掉首尾静音、统一响度，
编码为 16 kHz 4 bit IMA-ADPCM，写出 `assets/audio/generated/kopack.bin`：

```bash
pip install edge-tts           # 建议在虚拟环境里安装
python3 tools/korean_audio.py synth    # 约 256 条，按文本缓存
python3 tools/korean_audio.py pack     # 需要 ffmpeg
python3 tools/korean_audio.py verify
./tools/validate.sh --firmware
```

存在 `kopack.bin` 时，构建会把它烧到 `kopack` 分区并放进合并固件；没有它固件照样能构建和运行，只是没有声音：
喇叭图标和"听音选词"会隐藏 / 置灰，设置页显示"未安装"。生成的音频来自第三方在线服务，再分发前请确认其条款，
因此 `assets/audio/generated/` 被 Git 忽略。

## 分区布局

| 分区 | 偏移 | 大小 | 用途 |
| --- | ---: | ---: | --- |
| `nvs` | `0x9000` | `0x6000` | 学习进度与设置 |
| `phy_init` | `0xF000` | `0x1000` | PHY 数据 |
| `factory` | `0x10000` | `0x2F0000` | 应用（实际约 1.2 MB） |
| `kopack` | `0x300000` | `0x500000` | 发音包（data，subtype `0x40`） |

## 修改学习内容

界面上的所有文字都来自 [`assets/korean/content.json`](assets/korean/content.json)。新条目请追加在列表末尾，
已保存的进度才能继续对得上。改完后重新生成数据表、字符串和字体子集：

```bash
python3 tools/gen_korean_assets.py generate --fonts \
  --lv-font-conv <lv_font_conv-1.5.3 路径> \
  --font-sc <SourceHanSansSC-Regular.otf> --font-kr <SourceHanSansKR-Regular.otf>
```

生成物过期、任何显示的字符在绘制它的字体里缺字形、或应用代码绕过 `main/ko_strings.h` 直接写中文 / 韩文时，
静态门禁都会失败。字体来源与许可见 [`assets/README.zh_CN.md`](assets/README.zh_CN.md)。

## 设计要点

- 板级访问只通过 BSP（`bsp_display_*`、`bsp_lvgl_*`、`bsp_button_*`、`bsp_audio_*`、`bsp_battery_*`）。
  基线的 `demo_*.c` 与 `ui_pixel*` 仍留在 `main/` 供它们的主机测试使用，但不参与链接。
- 纯逻辑（`main/ko_*.c` 中除 `ko_app`、`ko_audio`、`ko_store`、`ko_ui*` 以外的文件）不依赖 ESP-IDF，
  由主机测试覆盖；`ko_view.c` 生成每个页面的显示模型，固件、主机预览和测试共用同一份。
- 按键回调只入队；输入任务更新状态并在 `bsp_lvgl_lock()` 下刷新界面；音频任务独占 PCM 写入、从不碰 LVGL；
  状态任务负责调暗、电量和存档。
- NVS 写入在最后一次改动 1.5 秒后才进行，且播放声音时不写，因为 Flash 写入会让 I2S DMA 断粮。
- LVGL 内存池为 32 KB（基线 24 KB），并关闭了默认主题。`tools/render_korean_preview.py` 在主机上用真实
  LVGL 和应用界面代码渲染每个页面，实测峰值约 22.9 KB，超过池的 75% 时脚本失败。

## 已知限制

- 熄屏只是调暗 / 关闭背光；BSP 没有按键唤醒的休眠接口，熄屏期间 CPU 仍在运行。
- 汉语读音提示只是给初学者的近似。
- 罗马音采用韩国国语院修订罗马字，按实际发音转写，不标出紧音化。
