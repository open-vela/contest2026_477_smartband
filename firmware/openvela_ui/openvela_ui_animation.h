/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENVELA_UI_ANIMATION_H
#define OPENVELA_UI_ANIMATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MOONCAT_FRAME_CACHE_SLOTS 64U
#define MOONCAT_FRAME_CACHE_BUDGET (2U * 1024U * 1024U)
struct mooncat_frame_cache_s {
    struct { char path[128]; void *pixels; size_t bytes; } entries[MOONCAT_FRAME_CACHE_SLOTS];
    size_t count;
    size_t bytes;
    bool allocation_failed;
};

static inline void mooncat_frame_cache_clear(struct mooncat_frame_cache_s *cache,
                                             void (*release)(void *))
{
    for (size_t i = 0; i < cache->count; i++) release(cache->entries[i].pixels);
    memset(cache, 0, sizeof(*cache));
}

static inline bool mooncat_frame_cache_get(const struct mooncat_frame_cache_s *cache,
                                           const char *path, void *out, size_t bytes)
{
    for (size_t i = 0; i < cache->count; i++) {
        if (cache->entries[i].bytes == bytes && !strcmp(cache->entries[i].path, path)) {
            memcpy(out, cache->entries[i].pixels, bytes);
            return true;
        }
    }
    return false;
}

static inline void mooncat_frame_cache_put(struct mooncat_frame_cache_s *cache,
    const char *path, const void *pixels, size_t bytes, void *(*allocate)(size_t))
{
    void *copy;
    if (!bytes || cache->allocation_failed || cache->count == MOONCAT_FRAME_CACHE_SLOTS ||
        bytes > MOONCAT_FRAME_CACHE_BUDGET - cache->bytes || strlen(path) >= 128) return;
    for (size_t i = 0; i < cache->count; i++) {
        if (!strcmp(cache->entries[i].path, path)) return;
    }
    copy = allocate(bytes);
    if (!copy) { cache->allocation_failed = true; return; }
    memcpy(copy, pixels, bytes);
    strcpy(cache->entries[cache->count].path, path);
    cache->entries[cache->count].pixels = copy;
    cache->entries[cache->count].bytes = bytes;
    cache->count++;
    cache->bytes += bytes;
}

struct mooncat_animation_sample_s {
    uint16_t current;
    uint16_t next;
    uint8_t blend;
};

/* Schedule on the action's absolute time grid. Relative 16/17 ms timers
 * accumulate every late wakeup and run at roughly 57 Hz on this board.
 * Skip missed deadlines rather than replaying frames in a busy catch-up loop. */
static inline uint32_t mooncat_animation_delay(uint32_t elapsed, uint32_t fps)
{
    if (!fps || fps > 1000U) return 1U;
    uint64_t frame = (uint64_t)elapsed * fps / 1000U + 1U;
    uint64_t deadline = (frame * 1000U + fps - 1U) / fps;
    return (uint32_t)(deadline - elapsed);
}

/* Repeated paths encode a longer source-frame interval. Interpolate across
 * that complete interval, preserving its boundaries and the cycle duration,
 * instead of holding a frame until only the last repeated slot remains. */
static inline bool mooncat_animation_sample(const char *const *frames,
    uint16_t count, uint32_t duration, uint32_t elapsed,
    struct mooncat_animation_sample_s *sample)
{
    uint64_t phase;
    uint16_t first, end;
    if (!frames || !count || !duration || !sample) return false;
    phase = (uint64_t)(elapsed % duration) * count * 256U / duration;
    first = (uint16_t)(phase >> 8);
    end = first + 1U;
    while (first && strcmp(frames[first - 1U], frames[first]) == 0) first--;
    while (end < count && strcmp(frames[end], frames[first]) == 0) end++;
    sample->current = first;
    sample->next = end % count;
    sample->blend = (uint8_t)((phase - (uint32_t)first * 256U) / (end - first));
    return true;
}

/* Two independent 16-bit lanes: each weighted byte sum is <= 65025.
 * The rounding correction stays below 65536, so no lane carry occurs.
 * This is bit-exact with the original per-channel rounded division by 255;
 * inputs and output are premultiplied BGRA bytes on this little-endian target. */
static inline uint32_t mooncat_blend_lanes(uint32_t a, uint32_t b, uint8_t blend)
{
    uint32_t value = a * (255U - blend) + b * blend + 0x00800080U;
    return ((value + ((value >> 8) & 0x00ff00ffU)) >> 8) & 0x00ff00ffU;
}

static inline uint32_t mooncat_blend_argb8888(uint32_t a, uint32_t b, uint8_t blend)
{
    if (a == b || blend == 0) return a;
    if (blend == 255) return b;
    return mooncat_blend_lanes(a & 0x00ff00ffU, b & 0x00ff00ffU, blend) |
        (mooncat_blend_lanes((a >> 8) & 0x00ff00ffU,
                            (b >> 8) & 0x00ff00ffU, blend) << 8);
}

#endif
