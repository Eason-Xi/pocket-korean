// main/ko_pack.h —— 发音包（kopack 分区里的 ADPCM 发音集合）的格式解析。
// 通过读取回调访问数据，所以在主机上可以直接用内存 / 文件测试，固件里接 esp_partition_read。
//
// 布局（全部小端）：
//   头 40 字节：
//     0  "KOPK"        魔数
//     4  version u16   = 1
//     6  header_size u16 = 40
//     8  clip_count u32
//     12 sample_rate u32   = 16000
//     16 index_offset u32  = 40
//     20 data_offset u32   = 40 + 12 * clip_count
//     24 total_size u32    整个包占用的字节数（<= 分区大小）
//     28 index_crc32 u32   索引区的 CRC-32
//     32 reserved u32      = 0
//     36 header_crc32 u32  头前 36 字节的 CRC-32
//   索引：clip_count 项，每项 12 字节：offset u32（相对 data_offset）、bytes u32、samples u32；
//         bytes == 0 表示这条发音缺失（生成失败），播放时当作没有发音。
//   数据：每条发音是 ko_adpcm.h 描述的 ADPCM 流。
//
// 只校验头和索引的 CRC（开机读几 KB）；不校验整包，否则每次开机要读完 3 MB。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KO_PACK_HEADER_BYTES 40
#define KO_PACK_ENTRY_BYTES 12
#define KO_PACK_VERSION 1
#define KO_PACK_SAMPLE_RATE 16000
#define KO_PACK_MAX_CLIPS 4096

// 从包内偏移 offset 读 len 字节到 dst；成功返回 true。
typedef bool (*ko_pack_read_fn)(void *ctx, uint32_t offset, void *dst, uint32_t len);

typedef struct {
    ko_pack_read_fn read;
    void *ctx;
    uint32_t capacity;   // 可读的总字节数（分区大小）
} ko_pack_reader_t;

typedef struct {
    uint32_t clip_count;
    uint32_t sample_rate;
    uint32_t index_offset;
    uint32_t data_offset;
    uint32_t total_size;
} ko_pack_info_t;

typedef struct {
    uint32_t offset;    // 在整个包里的绝对偏移
    uint32_t bytes;     // ADPCM 流字节数（含 4 字节头）
    uint32_t samples;   // 解出的采样数
} ko_pack_clip_t;

typedef enum {
    KO_PACK_OK = 0,
    KO_PACK_EMPTY,   // 分区是空的（擦除态 0xFF）：没有安装发音包，属于正常情况
    KO_PACK_BAD,     // 有内容但头 / 索引不合法：包损坏或版本不认识
} ko_pack_status_t;

ko_pack_status_t ko_pack_open(const ko_pack_reader_t *reader, ko_pack_info_t *info);

// 取第 id 条发音的位置。id 越界、条目缺失或条目不合法返回 false。
bool ko_pack_clip(const ko_pack_reader_t *reader, const ko_pack_info_t *info, uint32_t id,
                  ko_pack_clip_t *out);
