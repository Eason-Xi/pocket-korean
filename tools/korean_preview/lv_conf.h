// tools/korean_preview/lv_conf.h —— 主机预览用的 LVGL 最小配置。
//
// 关键取值与固件保持一致（sdkconfig.defaults）：16 位色、32 KB 内置内存池、
// 20 ms 刷新周期、Montserrat 14/20。这样预览里出现的"内存池耗尽"就是固件里也会出现的问题。
// 主机是 64 位，对象里的指针更大，所以预览测得的内存占用比板上的真实值偏高（偏保守）。
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

#ifndef KO_PREVIEW_MEM_KB
#define KO_PREVIEW_MEM_KB 32
#endif
#define LV_MEM_SIZE (KO_PREVIEW_MEM_KB * 1024U)

#define LV_DEF_REFR_PERIOD 20
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// 缺字形时显示占位框：预览里一眼就能看见"豆腐块"，不能被隐藏。
#define LV_USE_FONT_PLACEHOLDER 1

// 应用的每个对象都 lv_obj_remove_style_all() 后自己带样式，用不到默认主题；
// 主题在初始化时要在内存池里建几十个样式，关掉能省下几 KB（固件里同样关闭，见 sdkconfig.defaults）。
#ifndef KO_PREVIEW_THEME
#define KO_PREVIEW_THEME 0
#endif
#define LV_USE_THEME_DEFAULT KO_PREVIEW_THEME

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 1

#endif /* LV_CONF_H */
