// main/ko_pack.c —— 发音包头与索引解析。
#include "ko_pack.h"

#include <string.h>

#include "ko_crc32.h"

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

// 一个索引项是否自洽：不越过数据区，采样数与字节数匹配。
static bool entry_valid(const ko_pack_info_t *info, uint32_t offset, uint32_t bytes,
                        uint32_t samples) {
    if (bytes == 0) return samples == 0;   // 缺失项
    if (bytes < 5) return false;           // 至少 4 字节头 + 1 字节数据
    const uint32_t data_size = info->total_size - info->data_offset;
    if (offset > data_size || bytes > data_size - offset) return false;
    // 头里的预测值是第 0 个采样，其余每字节两个码。
    return samples >= 1 && samples <= 1u + (bytes - 4u) * 2u;
}

ko_pack_status_t ko_pack_open(const ko_pack_reader_t *reader, ko_pack_info_t *info) {
    uint8_t header[KO_PACK_HEADER_BYTES];
    if (reader->capacity < KO_PACK_HEADER_BYTES ||
        !reader->read(reader->ctx, 0, header, sizeof(header))) {
        return KO_PACK_BAD;
    }

    if (memcmp(header, "KOPK", 4) != 0) {
        // 擦除态的 Flash 全是 0xFF：分区没烧过包，属于正常的"未安装"。
        for (int i = 0; i < 4; i++) {
            if (header[i] != 0xFF) return KO_PACK_BAD;
        }
        return KO_PACK_EMPTY;
    }

    if (get_u16(header + 4) != KO_PACK_VERSION ||
        get_u16(header + 6) != KO_PACK_HEADER_BYTES ||
        ko_crc32(header, 36) != get_u32(header + 36)) {
        return KO_PACK_BAD;
    }

    ko_pack_info_t parsed = {
        .clip_count = get_u32(header + 8),
        .sample_rate = get_u32(header + 12),
        .index_offset = get_u32(header + 16),
        .data_offset = get_u32(header + 20),
        .total_size = get_u32(header + 24),
    };
    const uint32_t index_crc = get_u32(header + 28);

    if (parsed.clip_count == 0 || parsed.clip_count > KO_PACK_MAX_CLIPS) return KO_PACK_BAD;
    if (parsed.sample_rate != KO_PACK_SAMPLE_RATE) return KO_PACK_BAD;
    if (parsed.index_offset != KO_PACK_HEADER_BYTES) return KO_PACK_BAD;
    if (parsed.data_offset != parsed.index_offset + parsed.clip_count * KO_PACK_ENTRY_BYTES) {
        return KO_PACK_BAD;
    }
    if (parsed.total_size < parsed.data_offset || parsed.total_size > reader->capacity) {
        return KO_PACK_BAD;
    }

    // 分批读索引：既算 CRC 又逐项检查，栈上只放一小块。
    uint8_t chunk[8 * KO_PACK_ENTRY_BYTES];
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t done = 0; done < parsed.clip_count;) {
        uint32_t n = parsed.clip_count - done;
        if (n > 8) n = 8;
        if (!reader->read(reader->ctx, parsed.index_offset + done * KO_PACK_ENTRY_BYTES, chunk,
                          n * KO_PACK_ENTRY_BYTES)) {
            return KO_PACK_BAD;
        }
        crc = ko_crc32_update(crc, chunk, n * KO_PACK_ENTRY_BYTES);
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *e = chunk + i * KO_PACK_ENTRY_BYTES;
            if (!entry_valid(&parsed, get_u32(e), get_u32(e + 4), get_u32(e + 8))) {
                return KO_PACK_BAD;
            }
        }
        done += n;
    }
    if (~crc != index_crc) return KO_PACK_BAD;

    *info = parsed;
    return KO_PACK_OK;
}

bool ko_pack_clip(const ko_pack_reader_t *reader, const ko_pack_info_t *info, uint32_t id,
                  ko_pack_clip_t *out) {
    if (id >= info->clip_count) return false;
    uint8_t entry[KO_PACK_ENTRY_BYTES];
    if (!reader->read(reader->ctx, info->index_offset + id * KO_PACK_ENTRY_BYTES, entry,
                      sizeof(entry))) {
        return false;
    }
    const uint32_t offset = get_u32(entry);
    const uint32_t bytes = get_u32(entry + 4);
    const uint32_t samples = get_u32(entry + 8);
    if (bytes == 0 || !entry_valid(info, offset, bytes, samples)) return false;

    out->offset = info->data_offset + offset;
    out->bytes = bytes;
    out->samples = samples;
    return true;
}
