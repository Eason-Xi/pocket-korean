// main/ko_theme.h —— 视觉主题："韩纸与墨"。
//
// 暖色纸底 + 藏青页眉，强调色取自太极旗的红与蓝。大字号、高对比、少装饰：
// 这块屏只有 240×320，学习内容要一眼看清；同时控制 LVGL 对象数量
// （内置内存池只有 24 KB），不用阴影、渐变、动画。
#pragma once

#define KO_COL_PAPER   0xF4EFE4   // 页面底色
#define KO_COL_CARD    0xFFFDF8   // 卡片 / 选中行底色
#define KO_COL_LINE    0xD9CEB8   // 卡片描边
#define KO_COL_INK     0x1F2430   // 正文
#define KO_COL_SUB     0x6B7280   // 次要文字
#define KO_COL_NAVY    0x1C2B4A   // 页眉
#define KO_COL_NAVY_TX 0xF4EFE4   // 页眉文字
#define KO_COL_FOOTER  0xE8E0CF   // 页脚底色
#define KO_COL_RED     0xC8323C   // 强调 / 选中
#define KO_COL_BLUE    0x1F4E9E
#define KO_COL_TEAL    0x2E7D6B
#define KO_COL_GOLD    0xB9832A
#define KO_COL_GRAY    0x8A8F98
#define KO_COL_OK      0x2E8B57   // 答对
#define KO_COL_OK_BG   0xDDF0E4
#define KO_COL_BAD     0xC8323C   // 答错
#define KO_COL_BAD_BG  0xF8DDDD
