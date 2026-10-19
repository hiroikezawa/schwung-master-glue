/*
 * MASTER GLUE - Schwung audio FX v2 adapter.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Every entry point here runs on Move's SPI callback (or, for a bus insert,
 * the bus worker): no allocation, no file I/O, no locks. Instances come from
 * a static pool claimed with an atomic compare-and-swap, so even
 * create_instance allocates nothing.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "host/audio_fx_api_v2.h"
#include "mg_engine.h"
#include "mg_metadata.h"

#define MG_POOL 8

typedef struct {
    int used;
    mg_engine engine;
} mg_slot;

static mg_slot g_pool[MG_POOL];
static int g_host_rate = 44100;

static int param_index(const char *key) {
    for (int i = 0; i < MG_P_COUNT; ++i)
        if (!strcmp(key, MG_PARAMS[i].key)) return i;
    return -1;
}

static const char *skip_space(const char *p) {
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

static const char *const *enum_names(int index, int *count) {
    switch (index) {
    case MG_P_RELEASE: *count = MG_RELEASE_COUNT; return MG_RELEASE_NAMES;
    case MG_P_LOW_PROTECT: *count = MG_HPF_COUNT; return MG_HPF_NAMES;
    case MG_P_RATIO: *count = MG_RATIO_COUNT; return MG_RATIO_NAMES;
    case MG_P_COMP_BYPASS:
    case MG_P_LIM_BYPASS:
    case MG_P_BYPASS:
    case MG_P_COMPARE: *count = 2; return MG_SWITCH_NAMES;
    default: *count = 0; return NULL;
    }
}

/* A value is a number, or for an enum one of its option names (any case).
 * Names are tried first: "1.5:1" would otherwise parse as the number 1.5. */
static int parse_value(int index, const char *text, float *out) {
    const char *p = skip_space(text);
    int count;
    const char *const *names = enum_names(index, &count);
    for (int i = 0; i < count; ++i) {
        size_t n = strlen(names[i]);
        if (!strncasecmp(p, names[i], n) && !*skip_space(p + n)) {
            *out = (float)i;
            return 1;
        }
    }
    char *end;
    float v = strtof(p, &end);
    if (end == p || *skip_space(end) || !isfinite(v)) return 0;
    *out = v;
    return 1;
}

/* Flat JSON object of numbers. Known keys are applied, unknown ones (from a
 * later version) skipped; a malformed document changes nothing. */
static int restore_state(mg_engine *e, const char *text) {
    if (strlen(text) > 4096) return 0;
    float next[MG_P_COUNT];
    unsigned seen = 0;
    int preset = -1;
    const char *p = skip_space(text);
    if (*p++ != '{') return 0;
    p = skip_space(p);
    if (*p == '}') return 0;
    for (int field = 0; field < 64; ++field) {
        p = skip_space(p);
        if (*p++ != '"') return 0;
        char key[32];
        int n = 0;
        while (*p && *p != '"' && n < (int)sizeof(key) - 1) key[n++] = *p++;
        key[n] = 0;
        if (*p++ != '"') return 0;
        p = skip_space(p);
        if (*p++ != ':') return 0;
        p = skip_space(p);
        char *end;
        float v = strtof(p, &end);
        if (end == p || !isfinite(v)) return 0;
        p = end;
        int idx = param_index(key);
        if (idx >= 0) {
            next[idx] = v;
            seen |= 1u << idx;
        } else if (!strcmp(key, "preset")) {
            preset = (int)v;
        }
        p = skip_space(p);
        if (*p == '}') {
            if (*skip_space(p + 1)) return 0;
            for (int i = 0; i < MG_P_COUNT; ++i)
                if ((seen & (1u << i)) && i != MG_P_THRESHOLD) mg_engine_set(e, i, next[i]);
            if (!(seen & (1u << MG_P_GLUE)) && (seen & (1u << MG_P_THRESHOLD)))
                mg_engine_set(e, MG_P_THRESHOLD, next[MG_P_THRESHOLD]);
            if (preset >= 0 && preset < MG_PRESET_COUNT) e->preset = preset;
            return 1;
        }
        if (*p++ != ',') return 0;
    }
    return 0;
}

static int copy_text(char *buf, int size, const char *text) {
    size_t n = strlen(text);
    if (n >= (size_t)size) {
        buf[0] = 0;
        return -1;
    }
    memcpy(buf, text, n + 1);
    return (int)n;
}

static void *create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json;
    for (int i = 0; i < MG_POOL; ++i) {
        int expected = 0;
        if (__atomic_compare_exchange_n(&g_pool[i].used, &expected, 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            mg_engine_init(&g_pool[i].engine, (float)g_host_rate);
            return &g_pool[i].engine;
        }
    }
    return NULL;
}

static void destroy_instance(void *instance) {
    for (int i = 0; i < MG_POOL; ++i) {
        if (instance == &g_pool[i].engine) {
            __atomic_store_n(&g_pool[i].used, 0, __ATOMIC_RELEASE);
            return;
        }
    }
}

static void process_block(void *instance, int16_t *audio, int frames) {
    if (!instance || !audio || frames <= 0) return;
    mg_engine_process((mg_engine *)instance, audio, frames);
}

static void set_param(void *instance, const char *key, const char *val) {
    if (!instance || !key || !val) return;
    mg_engine *e = (mg_engine *)instance;
    if (!strcmp(key, "state")) {
        (void)restore_state(e, val);
        return;
    }
    if (!strcmp(key, "preset")) {
        float v;
        const char *p = skip_space(val);
        char *end;
        v = strtof(p, &end);
        if (end != p && isfinite(v)) mg_engine_load_preset(e, (int)floorf(v + 0.5f));
        return;
    }
    int idx = param_index(key);
    float v;
    if (idx < 0 || !parse_value(idx, val, &v)) return;
    mg_engine_set(e, idx, v);
}

static int fmt(char *buf, int size, const char *f, double v) {
    int n = snprintf(buf, size, f, v);
    if (n < 0 || n >= size) {
        buf[0] = 0;
        return -1;
    }
    return n;
}

static int get_param(void *instance, const char *key, char *buf, int size) {
    if (!instance || !key || !buf || size <= 0) return -1;
    buf[0] = 0;
    const mg_engine *e = (const mg_engine *)instance;
    int idx = param_index(key);
    if (idx >= 0) {
        int count;
        const char *const *names = enum_names(idx, &count);
        if (names) return copy_text(buf, size, names[(int)e->p[idx]]);
        return fmt(buf, size, "%.4g", e->p[idx]);
    }
    if (!strcmp(key, "in_peak")) return fmt(buf, size, "%.1f", mg_meter_in(e));
    if (!strcmp(key, "out_peak")) return fmt(buf, size, "%.1f", mg_meter_out(e));
    if (!strcmp(key, "comp_gr")) return fmt(buf, size, "%.1f", mg_meter_comp_gr(e));
    if (!strcmp(key, "lim_gr")) return fmt(buf, size, "%.1f", mg_meter_lim_gr(e));
    if (!strcmp(key, "preset")) return fmt(buf, size, "%.0f", e->preset);
    if (!strcmp(key, "preset_count")) return fmt(buf, size, "%.0f", MG_PRESET_COUNT);
    if (!strcmp(key, "preset_name")) return copy_text(buf, size, mg_preset_name(e->preset));
    if (!strcmp(key, "name")) return copy_text(buf, size, "MASTER GLUE");
    if (!strcmp(key, "chain_params")) return copy_text(buf, size, kChainParams);
    if (!strcmp(key, "ui_hierarchy")) return copy_text(buf, size, kUiHierarchy);
    if (!strcmp(key, "version")) return copy_text(buf, size, kVersion);
    if (!strcmp(key, "latency_samples")) return fmt(buf, size, "%.0f", mg_latency_samples(e));
    if (!strcmp(key, "nan_resets")) return fmt(buf, size, "%.0f", e->nan_resets);
    if (!strcmp(key, "state")) {
        /* THRESHOLD is not saved: it is GLUE in another unit. */
        char tmp[1024];
        int off = snprintf(tmp, sizeof tmp, "{\"version\":1,\"preset\":%d", e->preset);
        for (int i = 0; i < MG_P_COUNT && off > 0 && off < (int)sizeof tmp; ++i) {
            if (i == MG_P_THRESHOLD) continue;
            off += snprintf(tmp + off, sizeof tmp - off, ",\"%s\":%.6g", MG_PARAMS[i].key, e->p[i]);
        }
        if (off <= 0 || off >= (int)sizeof tmp - 2) return -1;
        tmp[off++] = '}';
        tmp[off] = 0;
        return copy_text(buf, size, tmp);
    }
    return -1;
}

static audio_fx_api_v2_t g_api = {
    AUDIO_FX_API_VERSION_2,
    create_instance,
    destroy_instance,
    process_block,
    set_param,
    get_param,
    NULL,
};

__attribute__((visibility("default")))
audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    if (host && host->sample_rate > 0) {
        if (host->sample_rate != 44100 && host->sample_rate != 48000) return NULL;
        g_host_rate = host->sample_rate;
    }
    return &g_api;
}
