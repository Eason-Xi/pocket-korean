// main/main.c —— 韩语学习应用入口：复用 BSP 完成板级初始化，然后交给 ko_app。
//
// 这里不含任何界面：基线的 demo 菜单 / 页面（demo_*.c、ui_pixel*）只是硬件能力演示，
// 派生应用必须有自己的界面，所以本应用的页面全部在 ko_ui*.c 里重新设计。
// 基线 demo 的源文件仍留在 main/ 里供参考和主机测试使用，但不参与固件构建（见 CMakeLists.txt）。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"   // 错误日志里要打印 BSP_LCD_* 引脚号
#include "esp_log.h"

#include "ko_app.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "韩语学习 启动");

    // 共享 I2C 总线由 BSP 独占持有；音频与电量计的 init 也会幂等地调用它，这里先建好并扫描，
    // 出问题时日志里能看到 0x18（ES8311）/ 0x63（CW2017）是否应答。
    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是这个应用的载体，没有它无法使用：打清楚日志后退出，不做串口降级。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示 / LVGL 初始化失败，应用无法继续。检查 SPI 接线"
                      "（MOSI=%d SCLK=%d CS=%d DC=%d BL=%d）",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }

    // 音频与电量计是可选外设：失败只降级（无声 / 不显示电量），不阻塞学习功能。
    const bool audio_ok = bsp_audio_init() == ESP_OK;
    const bool battery_ok = bsp_battery_init() == ESP_OK;
    if (!audio_ok) ESP_LOGW(TAG, "音频初始化失败：应用将无声运行");
    if (!battery_ok) ESP_LOGW(TAG, "电量计不可用：不显示电量");

    const esp_err_t err = ko_app_start(audio_ok, battery_ok);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "应用启动失败: %s", esp_err_to_name(err));
    }
}
