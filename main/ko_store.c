// main/ko_store.c —— NVS 持久化。
#include "ko_store.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "ko_store";

#define NAMESPACE "kolearn"
#define KEY_PROGRESS "prog"
#define KEY_VOLUME "vol"
#define KEY_BRIGHT "bri"

#define DEFAULT_VOLUME 60
#define DEFAULT_BRIGHTNESS 80

static ko_progress_t s_progress;
static ko_settings_t s_settings = { DEFAULT_VOLUME, DEFAULT_BRIGHTNESS };
static SemaphoreHandle_t s_lock;
static bool s_ok;
static bool s_dirty;
static uint32_t s_dirty_since_ms;

static void clamp_settings(ko_settings_t *s) {
    if (s->volume > 100) s->volume = 100;
    if (s->brightness < 10) s->brightness = 10;
    if (s->brightness > 100) s->brightness = 100;
}

esp_err_t ko_store_init(void) {
    memset(&s_progress, 0, sizeof(s_progress));
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        // 不擦除：NO_FREE_PAGES / NEW_VERSION_FOUND 通常意味着分区里有别的内容，
        // 擦掉会丢用户数据。退化为本次运行不保存。
        ESP_LOGE(TAG, "nvs_flash_init 失败: %s；本次运行的进度不会保存", esp_err_to_name(err));
        return err;
    }
    nvs_handle_t handle;
    err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open 失败: %s；本次运行的进度不会保存", esp_err_to_name(err));
        return err;
    }
    s_ok = true;

    uint8_t buf[KO_PROGRESS_MAX_BYTES + 16];
    size_t len = sizeof(buf);
    err = nvs_get_blob(handle, KEY_PROGRESS, buf, &len);
    if (err == ESP_OK) {
        if (!ko_progress_parse(&s_progress, buf, len)) {
            // 存档损坏：从头开始。不立刻覆盖，等用户产生新进度时再写，
            // 这样万一是读取时序问题，下次开机还有机会读回原数据。
            ESP_LOGW(TAG, "进度存档校验失败（%u 字节），从头开始", (unsigned)len);
            memset(&s_progress, 0, sizeof(s_progress));
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "读取进度失败: %s", esp_err_to_name(err));
    }

    uint8_t value;
    if (nvs_get_u8(handle, KEY_VOLUME, &value) == ESP_OK) s_settings.volume = value;
    if (nvs_get_u8(handle, KEY_BRIGHT, &value) == ESP_OK) s_settings.brightness = value;
    clamp_settings(&s_settings);
    nvs_close(handle);
    return ESP_OK;
}

bool ko_store_ok(void) {
    return s_ok;
}

ko_progress_t *ko_store_progress(void) {
    return &s_progress;
}

void ko_store_lock(void) {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

void ko_store_unlock(void) {
    if (s_lock) xSemaphoreGive(s_lock);
}

void ko_store_mark_dirty(uint32_t now_ms) {
    s_dirty = true;
    s_dirty_since_ms = now_ms;   // 每次改动都重新计时：连续操作合并成一次写
}

ko_settings_t ko_store_settings(void) {
    return s_settings;
}

void ko_store_set_settings(ko_settings_t settings, uint32_t now_ms) {
    clamp_settings(&settings);
    ko_store_lock();
    s_settings = settings;
    ko_store_unlock();
    ko_store_mark_dirty(now_ms);
}

void ko_store_reset_progress(uint32_t now_ms) {
    ko_store_lock();
    memset(&s_progress, 0, sizeof(s_progress));
    ko_store_unlock();
    ko_store_mark_dirty(now_ms);
    ko_store_flush();
}

// 快照 -> 写盘。写 Flash 期间不持有互斥锁，输入任务不会被阻塞。
static bool write_now(void) {
    if (!s_ok) {
        s_dirty = false;   // 没有持久化就别一直重试
        return false;
    }
    uint8_t buf[KO_PROGRESS_MAX_BYTES];
    ko_settings_t settings;
    ko_store_lock();
    const size_t len = ko_progress_serialize(&s_progress, buf, sizeof(buf));
    settings = s_settings;
    s_dirty = false;
    ko_store_unlock();
    if (len == 0) return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, KEY_PROGRESS, buf, len);
        if (err == ESP_OK) err = nvs_set_u8(handle, KEY_VOLUME, settings.volume);
        if (err == ESP_OK) err = nvs_set_u8(handle, KEY_BRIGHT, settings.brightness);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存失败: %s", esp_err_to_name(err));
        s_dirty = true;   // 下次再试
        return false;
    }
    return true;
}

bool ko_store_tick(uint32_t now_ms, bool audio_busy) {
    if (!s_dirty || audio_busy) return false;
    if ((uint32_t)(now_ms - s_dirty_since_ms) < KO_STORE_SETTLE_MS) return false;
    return write_now();
}

void ko_store_flush(void) {
    if (s_dirty) (void)write_now();
}
