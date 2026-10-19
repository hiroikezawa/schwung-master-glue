/*
 * MASTER GLUE - stereo bus compressor + lookahead peak limiter.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * The engine is a plain struct with no pointers to heap memory: every buffer
 * is inline, so an instance is ready the moment it is zeroed and initialised.
 */
#ifndef MG_ENGINE_H
#define MG_ENGINE_H

#include <stdint.h>

#include "mg_params.h"

/* Lookahead is at most 3 ms at 48 kHz = 144 samples; rings are a power of
 * two comfortably above that. */
#define MG_RING 512
#define MG_RING_MASK (MG_RING - 1)
#define MG_LA_XFADE 256        /* samples to cross-fade a lookahead change */

#define MG_PRESET_COUNT 3

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1[2], z2[2];
} mg_hpf;

typedef struct {
    float fs;
    float p[MG_P_COUNT];      /* parameter values as set (THRESHOLD is derived) */
    int preset;               /* last preset loaded, for the browser */

    /* block-rate derived values */
    int hpf_index;
    mg_hpf hpf;
    float a_rms, a_att, a_rel, a_slow_att, a_slow_rel, a_lim_rel, a_smooth, a_ramp;
    int auto_release;

    /* smoothed controls (per sample) */
    float thr_s, makeup_s, limit_s, ceil_s, out_s;
    float comp_on, lim_on, wet, cmp;   /* 0..1 ramps */

    /* compressor state */
    float ms;                 /* sidechain mean square */
    float gr_fast, gr_slow;   /* gain reduction, dB (positive) */

    /* limiter state */
    float ring_z[MG_RING][2];   /* limiter input (post comp, post LIMIT gain) */
    float ring_x[MG_RING][2];   /* dry input, for the level-matched compare */
    float ring_req[MG_RING];    /* required gain per sample */
    float ring_rel[MG_RING];    /* post-release gain per sample */
    uint32_t dq_idx[MG_RING];   /* sliding-minimum deque (indices) */
    float dq_val[MG_RING];
    uint32_t dq_head, dq_tail;
    uint32_t n;                 /* sample counter */
    double box_sum;
    float rel_prev;
    int la;                     /* lookahead in samples (current) */
    int la_old;                 /* previous lookahead while cross-fading */
    int la_xfade;               /* samples of cross-fade remaining */
    uint32_t box_refresh;

    /* level-matched compare */
    double in_ms_acc, out_ms_acc;
    float in_ms, out_ms, match, match_s;

    /* meters: dB, held then falling */
    float m_in, m_out, m_cgr, m_lgr;
    float h_in, h_out, h_cgr, h_lgr;   /* hold time left, seconds */

    int nan_resets;
} mg_engine;

void mg_engine_init(mg_engine *e, float sample_rate);
/* Clears the audio state (delays, envelopes) and snaps the smoothed controls
 * to their targets; parameters are kept. */
void mg_engine_reset_state(mg_engine *e);
void mg_engine_set(mg_engine *e, int index, float value);
void mg_engine_load_preset(mg_engine *e, int preset);
const char *mg_preset_name(int preset);
void mg_engine_process(mg_engine *e, int16_t *audio, int frames);

/* What the meters show, all in dB: peaks in dBFS (floor -60), gain reduction
 * as a negative number (0 = none). */
float mg_meter_in(const mg_engine *e);
float mg_meter_out(const mg_engine *e);
float mg_meter_comp_gr(const mg_engine *e);
float mg_meter_lim_gr(const mg_engine *e);
int mg_latency_samples(const mg_engine *e);

#endif
