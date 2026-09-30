// main/ko_app.h —— 韩语学习应用的控制器：把按键、页面状态、界面、音频、存储串起来。
//
// 任务与锁：
//   * 按键回调（button 组件的 esp_timer 任务）只把事件放进队列，立刻返回；
//   * 输入任务取出事件，推进页面状态，并在持有 bsp_lvgl_lock() 时更新界面；
//   * 后台任务每 250 ms 醒一次：喇叭图标、电量、屏幕休眠、存档写入；
//   * 音频任务（ko_audio.c）从不碰 LVGL。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

// 在显示与 LVGL 已初始化后调用。
//   audio_ok    bsp_audio_init() 是否成功（失败则整个应用无声，但仍可学习）
//   battery_ok  bsp_battery_init() 是否成功（失败则不显示电量数字）
// 只有输入任务或界面无法创建时才返回错误；存储 / 音频不可用都只是降级。
esp_err_t ko_app_start(bool audio_ok, bool battery_ok);
