<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

### 韩语学习应用的字体子集

`fonts/ko_*.c` 是 LVGL 9.5 位图字体，由 [`tools/gen_korean_assets.py`](../tools/gen_korean_assets.py)
调用 `lv_font_conv` 1.5.3（`--no-compress --no-kerning`）生成。每个字体的字符集都从
[`korean/content.json`](korean/content.json) 精确推导，完整命令与源字体哈希记录在
[`fonts/ko_fonts.manifest.json`](fonts/ko_fonts.manifest.json)。`main/CMakeLists.txt` 编译全部 `ko_*.c`；
静态门禁会把它们的字符映射读回来，与内容不一致时失败。

| 文件 | 字号 / bpp | 字形数 | 源字体 | 用于 |
| --- | --- | ---: | --- | --- |
| `ko_zh14.c` | 14 px / 4 | 575 | 思源黑体 SC Regular | 小号中文、罗马音、按键提示 |
| `ko_zh20.c` | 20 px / 4 | 575 | 思源黑体 SC Regular | 标题、释义、测验选项 |
| `ko_ko24.c` | 24 px / 4 | 398 | 思源黑体 KR Regular | 全部韩文、徽标 |
| `ko_ko32.c` | 32 px / 4 | 324 | 思源黑体 KR Regular | 词汇与短语 |
| `ko_ko48.c` | 48 px / 2 | 330 | 思源黑体 KR Regular | 词汇卡片、测验题干、得分 |
| `ko_jamo96.c` | 96 px / 2 | 40 | 思源黑体 KR Regular | 字母卡片上的大字母 |

来源：[Adobe Source Han Sans（思源黑体）](https://github.com/adobe-fonts/source-han-sans) 2.005 的
`SubsetOTF/SC` 与 `SubsetOTF/KR`，采用 SIL Open Font License 1.1，允许再分发这些派生子集。
源 `.otf` 不入库，清单里记录了它们的 SHA-256。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 韩语学习内容（korean）

[`korean/content.json`](korean/content.json) 是韩语学习应用显示的全部文字的唯一来源：字母、词汇、短语和界面文字。
罗马音采用韩国国语院修订罗马字。新条目请追加在列表末尾，已保存的进度才能继续对得上，然后重新运行
`tools/gen_korean_assets.py`。内容为本仓库编写。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

韩语学习应用的发音包由 [`tools/korean_audio.py`](../tools/korean_audio.py) 调用 Microsoft Edge 在线朗读，
在本地生成到 `audio/generated/kopack.bin`（16 kHz 单声道、4 bit IMA-ADPCM）。其再分发条款尚未确认，
因此该目录被 Git 忽略。
