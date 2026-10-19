/*
 * MASTER GLUE - stereo bus compressor + lookahead peak limiter.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Signal path, per sample:
 *
 *   in -> [stereo-linked RMS compressor, sidechain HPF on the detector only]
 *      -> DENSITY (makeup) -> LIMIT (gain into the limiter)
 *      -> [lookahead peak limiter, ceiling = CEILING] -> OUTPUT (<= 0 dB) -> out
 *
 * OUTPUT is attenuation only, after the limiter, so it can never push the
 * signal past the ceiling.
 *
 * Everything here runs on Move's SPI callback: no allocation, no I/O, no
 * locks, and a bounded amount of work per sample.
 */
#include "mg_engine.h"

#include <math.h>
#include <string.h>

#define MG_PI 3.14159265358979f
#define MG_LN10_OVER_20 0.115129254649702f
/* log10 via logf: the toolchain's log10f binds to GLIBC_2.43, which the
 * Move (glibc <= 2.34) does not have. */
#define MG_INV_LN10 0.434294481903252f
static inline float mg_log10(float x) { return logf(x) * MG_INV_LN10; }

/* Fixed time constants (seconds). */
#define MG_RMS_TAU 0.005f        /* detector RMS averaging */
#define MG_AUTO_FAST_REL 0.12f   /* AUTO: release of the fast envelope */
#define MG_AUTO_SLOW_ATT 0.6f    /* AUTO: how slowly sustained GR charges the slow one */
#define MG_AUTO_SLOW_REL 1.2f    /* AUTO: release of the slow envelope */
#define MG_SMOOTH_TAU 0.02f      /* gain / threshold / ceiling smoothing */
#define MG_RAMP_S 0.01f          /* bypass / compare cross-fades */
#define MG_MATCH_TAU 3.0f        /* loudness integration for COMPARE */
#define MG_METER_HOLD 0.5f
#define MG_METER_FALL_DB 20.0f   /* dB per second */

typedef struct {
    const char *name;
    float ratio_index, punch, release_index, hpf_index, knee, ceiling;
} mg_preset;

static const mg_preset k_presets[MG_PRESET_COUNT] = {
    /*                ratio   attack release  HPF(index)       knee ceiling */
    {"TECHNO GLUE",  1.0f, 30.0f, 0.0f, 5.0f /* 100 Hz */, 6.0f, -1.0f},
    {"DEEP GLUE",    0.0f, 40.0f, 0.0f, 6.0f /* 120 Hz */, 6.0f, -1.0f},
    {"TIGHT TECHNO", 2.0f, 15.0f, 2.0f /* 100 ms */, 4.0f /* 90 Hz */, 6.0f, -1.0f},
};

static float coef(float seconds, float fs) {
    if (seconds <= 0.0f) return 1.0f;
    return 1.0f - expf(-1.0f / (seconds * fs));
}

static float db_to_lin(float db) { return expf(db * MG_LN10_OVER_20); }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int lookahead_samples(const mg_engine *e) {
    int la = (int)lrintf(e->p[MG_P_LOOKAHEAD] * 0.001f * e->fs);
    if (la < 1) la = 1;
    if (la > MG_RING / 2) la = MG_RING / 2;
    return la;
}

static void hpf_design(mg_hpf *h, float hz, float fs) {
    float w0 = 2.0f * MG_PI * hz / fs;
    float c = cosf(w0), alpha = sinf(w0) / (2.0f * 0.70710678f);
    float a0 = 1.0f + alpha;
    h->b0 = (1.0f + c) * 0.5f / a0;
    h->b1 = -(1.0f + c) / a0;
    h->b2 = h->b0;
    h->a1 = -2.0f * c / a0;
    h->a2 = (1.0f - alpha) / a0;
}

static inline float hpf_run(mg_hpf *h, int ch, float x) {
    float y = h->b0 * x + h->z1[ch];
    h->z1[ch] = h->b1 * x - h->a1 * y + h->z2[ch];
    h->z2[ch] = h->b2 * x - h->a2 * y;
    return y;
}

/* Soft-knee gain computer: gain reduction in dB (>= 0) for a detector level
 * x (dB), threshold t, ratio r and knee width w. */
static inline float gain_reduction(float x, float t, float r, float w) {
    float over = x - t;
    float slope = 1.0f - 1.0f / r;
    if (w > 0.0f && 2.0f * over > -w && 2.0f * over < w) {
        float k = over + 0.5f * w;
        return slope * k * k / (2.0f * w);
    }
    return over > 0.0f ? slope * over : 0.0f;
}

static float target_threshold(const mg_engine *e) { return mg_glue_to_threshold(e->p[MG_P_GLUE]); }
static float target_makeup(const mg_engine *e) { return db_to_lin(e->p[MG_P_DENSITY]); }
static float target_limit(const mg_engine *e) { return db_to_lin(e->p[MG_P_LIMIT]); }
static float target_ceiling(const mg_engine *e) { return db_to_lin(e->p[MG_P_CEILING]); }
static float target_output(const mg_engine *e) { return db_to_lin(e->p[MG_P_OUTPUT]); }

/* Rebuild the limiter's sliding minimum and box sum for a window of `la`
 * samples ending just before sample n, from the stored history. */
static void limiter_rebuild(mg_engine *e, int la) {
    e->dq_head = e->dq_tail = 0;
    for (uint32_t k = e->n - (uint32_t)la; k != e->n; ++k) {
        float v = e->ring_req[k & MG_RING_MASK];
        while (e->dq_tail != e->dq_head && e->dq_val[(e->dq_tail - 1) & MG_RING_MASK] >= v) e->dq_tail--;
        e->dq_idx[e->dq_tail & MG_RING_MASK] = k;
        e->dq_val[e->dq_tail & MG_RING_MASK] = v;
        e->dq_tail++;
    }
    double sum = 0.0;
    for (uint32_t k = e->n - (uint32_t)la; k != e->n; ++k) sum += e->ring_rel[k & MG_RING_MASK];
    e->box_sum = sum;
}

static void update_derived(mg_engine *e) {
    int hi = (int)e->p[MG_P_LOW_PROTECT];
    if (hi != e->hpf_index) {
        e->hpf_index = hi;
        if (hi > 0) hpf_design(&e->hpf, MG_HPF_HZ[hi], e->fs);
    }
    int ri = (int)e->p[MG_P_RELEASE];
    e->auto_release = (ri == 0);
    e->a_att = coef(e->p[MG_P_PUNCH] * 0.001f, e->fs);
    e->a_rel = coef(e->auto_release ? MG_AUTO_FAST_REL : MG_RELEASE_MS[ri] * 0.001f, e->fs);
    e->a_lim_rel = coef(e->p[MG_P_LIM_RELEASE] * 0.001f, e->fs);

    /* A lookahead change: the gain windows move at once, the audio tap
     * cross-fades so the delay change cannot click. One change at a time. */
    int la = lookahead_samples(e);
    if (la != e->la && e->la_xfade == 0) {
        e->la_old = e->la;
        e->la = la;
        e->la_xfade = MG_LA_XFADE;
        limiter_rebuild(e, la);
    }
}

void mg_engine_reset_state(mg_engine *e) {
    memset(&e->hpf.z1, 0, sizeof e->hpf.z1);
    memset(&e->hpf.z2, 0, sizeof e->hpf.z2);
    e->ms = 0.0f;
    e->gr_fast = e->gr_slow = 0.0f;
    memset(e->ring_z, 0, sizeof e->ring_z);
    memset(e->ring_x, 0, sizeof e->ring_x);
    for (int i = 0; i < MG_RING; ++i) e->ring_req[i] = e->ring_rel[i] = 1.0f;
    e->n = MG_RING;
    e->rel_prev = 1.0f;
    e->la = e->la_old = lookahead_samples(e);
    e->la_xfade = 0;
    e->box_refresh = 0;
    limiter_rebuild(e, e->la);

    e->thr_s = target_threshold(e);
    e->makeup_s = target_makeup(e);
    e->limit_s = target_limit(e);
    e->ceil_s = target_ceiling(e);
    e->out_s = target_output(e);
    e->comp_on = e->p[MG_P_COMP_BYPASS] >= 0.5f ? 0.0f : 1.0f;
    e->lim_on = e->p[MG_P_LIM_BYPASS] >= 0.5f ? 0.0f : 1.0f;
    e->wet = e->p[MG_P_BYPASS] >= 0.5f ? 0.0f : 1.0f;
    e->cmp = e->p[MG_P_COMPARE] >= 0.5f ? 1.0f : 0.0f;

    e->in_ms_acc = e->out_ms_acc = 0.0;
    e->in_ms = e->out_ms = 0.0f;
    e->match = e->match_s = 1.0f;
    e->m_in = e->m_out = -60.0f;
    e->m_cgr = e->m_lgr = 0.0f;
    e->h_in = e->h_out = e->h_cgr = e->h_lgr = 0.0f;
}

void mg_engine_init(mg_engine *e, float sample_rate) {
    memset(e, 0, sizeof *e);
    e->fs = sample_rate > 0.0f ? sample_rate : 44100.0f;
    for (int i = 0; i < MG_P_COUNT; ++i) e->p[i] = MG_PARAMS[i].def;
    e->preset = 0;
    e->hpf_index = -1;
    e->a_rms = coef(MG_RMS_TAU, e->fs);
    e->a_slow_att = coef(MG_AUTO_SLOW_ATT, e->fs);
    e->a_slow_rel = coef(MG_AUTO_SLOW_REL, e->fs);
    e->a_smooth = coef(MG_SMOOTH_TAU, e->fs);
    e->a_ramp = 1.0f / (MG_RAMP_S * e->fs);
    mg_engine_reset_state(e);
    update_derived(e);
}

void mg_engine_set(mg_engine *e, int index, float value) {
    if (index < 0 || index >= MG_P_COUNT || !isfinite(value)) return;
    const mg_param_info *pi = &MG_PARAMS[index];
    value = clampf(value, pi->min, pi->max);
    if (pi->is_enum) value = floorf(value + 0.5f);
    if (index == MG_P_THRESHOLD) {
        /* THRESHOLD is GLUE in dB: write the one stored value. */
        e->p[MG_P_GLUE] = clampf(mg_threshold_to_glue(value), 0.0f, 100.0f);
    } else {
        e->p[index] = value;
    }
    e->p[MG_P_THRESHOLD] = mg_glue_to_threshold(e->p[MG_P_GLUE]);
}

void mg_engine_load_preset(mg_engine *e, int preset) {
    if (preset < 0 || preset >= MG_PRESET_COUNT) return;
    const mg_preset *ps = &k_presets[preset];
    for (int i = 0; i < MG_P_COUNT; ++i) mg_engine_set(e, i, MG_PARAMS[i].def);
    mg_engine_set(e, MG_P_RATIO, ps->ratio_index);
    mg_engine_set(e, MG_P_PUNCH, ps->punch);
    mg_engine_set(e, MG_P_RELEASE, ps->release_index);
    mg_engine_set(e, MG_P_LOW_PROTECT, ps->hpf_index);
    mg_engine_set(e, MG_P_KNEE, ps->knee);
    mg_engine_set(e, MG_P_CEILING, ps->ceiling);
    e->preset = preset;
}

const char *mg_preset_name(int preset) {
    return (preset >= 0 && preset < MG_PRESET_COUNT) ? k_presets[preset].name : "";
}

static inline float ramp(float v, float target, float step) {
    if (v < target) return v + step > target ? target : v + step;
    if (v > target) return v - step < target ? target : v - step;
    return v;
}

static inline float smooth(float v, float target, float a) {
    float next = v + a * (target - v);
    /* Snap when close, and when the step has become too small to move the
     * float at all: otherwise the value can stall a few ulp short of the
     * target for good (a ceiling of -6 dB parked at -5.9995 dB). */
    return (next == v || fabsf(target - next) < 1e-6f) ? target : next;
}

static void meter(float *m, float *hold, float v, float dt) {
    if (v >= *m) {
        *m = v;
        *hold = MG_METER_HOLD;
    } else if (*hold > 0.0f) {
        *hold -= dt;
    } else {
        *m -= MG_METER_FALL_DB * dt;
        if (*m < v) *m = v;
    }
}

static float peak_db(float lin) {
    float db = lin > 0.001f ? 20.0f * mg_log10(lin) : -60.0f;
    return db < -60.0f ? -60.0f : db;
}

static int state_is_finite(const mg_engine *e) {
    return isfinite(e->ms) && isfinite(e->gr_fast) && isfinite(e->gr_slow) &&
           isfinite(e->rel_prev) && isfinite(e->box_sum) && isfinite(e->match_s) &&
           isfinite(e->hpf.z1[0]) && isfinite(e->hpf.z1[1]) &&
           isfinite(e->hpf.z2[0]) && isfinite(e->hpf.z2[1]);
}

void mg_engine_process(mg_engine *e, int16_t *audio, int frames) {
    update_derived(e);

    const float thr_t = target_threshold(e);
    const float makeup_t = target_makeup(e);
    const float limit_t = target_limit(e);
    const float ceil_t = target_ceiling(e);
    const float out_t = target_output(e);
    const float comp_t = e->p[MG_P_COMP_BYPASS] >= 0.5f ? 0.0f : 1.0f;
    const float lim_t = e->p[MG_P_LIM_BYPASS] >= 0.5f ? 0.0f : 1.0f;
    const float wet_t = e->p[MG_P_BYPASS] >= 0.5f ? 0.0f : 1.0f;
    const float cmp_t = e->p[MG_P_COMPARE] >= 0.5f ? 1.0f : 0.0f;
    const int comp_enabled = e->p[MG_P_GLUE] > 0.0f;
    const float ratio = MG_RATIO[(int)e->p[MG_P_RATIO]];
    const float knee = e->p[MG_P_KNEE];
    const int use_hpf = e->hpf_index > 0;
    const float a = e->a_smooth;

    float blk_in = 0.0f, blk_out = 0.0f, blk_cgr = 0.0f, blk_lg = 1.0f;
    double acc_in = 0.0, acc_out = 0.0;

    for (int i = 0; i < frames; ++i) {
        const float xl = audio[2 * i] * (1.0f / 32768.0f);
        const float xr = audio[2 * i + 1] * (1.0f / 32768.0f);

        e->thr_s = smooth(e->thr_s, thr_t, a);
        e->makeup_s = smooth(e->makeup_s, makeup_t, a);
        e->limit_s = smooth(e->limit_s, limit_t, a);
        e->ceil_s = smooth(e->ceil_s, ceil_t, a);
        e->out_s = smooth(e->out_s, out_t, a);
        e->match_s = smooth(e->match_s, e->match, a);
        e->comp_on = ramp(e->comp_on, comp_t, e->a_ramp);
        e->lim_on = ramp(e->lim_on, lim_t, e->a_ramp);
        e->wet = ramp(e->wet, wet_t, e->a_ramp);
        e->cmp = ramp(e->cmp, cmp_t, e->a_ramp);

        /* --- compressor: linked detector on the (high-passed) sidechain --- */
        float sl = xl, sr = xr;
        if (use_hpf) {
            sl = hpf_run(&e->hpf, 0, xl);
            sr = hpf_run(&e->hpf, 1, xr);
        }
        const float det = fmaxf(sl * sl, sr * sr);
        e->ms += e->a_rms * (det - e->ms);
        float target = 0.0f;
        if (comp_enabled) {
            const float lvl = 10.0f * mg_log10(e->ms + 1e-15f);
            target = gain_reduction(lvl, e->thr_s, ratio, knee);
        }
        e->gr_fast += (target > e->gr_fast ? e->a_att : e->a_rel) * (target - e->gr_fast);
        /* AUTO: a second, slow envelope follows the same target: it only
         * charges when reduction LASTS (0.6 s), and lets go over 1.2 s. The
         * gain follows whichever is deeper, so a short hit recovers on the
         * fast release (~0.12 s) while a sustained passage releases slowly
         * instead of pumping. Updated always, so switching modes does not
         * jump. */
        e->gr_slow += (target > e->gr_slow ? e->a_slow_att : e->a_slow_rel) * (target - e->gr_slow);
        float gr = e->auto_release ? fmaxf(e->gr_fast, e->gr_slow) : e->gr_fast;
        gr *= e->comp_on;
        const float gc = gr > 0.0f ? expf(-gr * MG_LN10_OVER_20) : 1.0f;
        if (gr > blk_cgr) blk_cgr = gr;

        /* --- makeup, gain into the limiter --- */
        const float g_pre = gc * e->makeup_s * e->limit_s;
        const float zl = xl * g_pre, zr = xr * g_pre;

        /* --- lookahead limiter --- */
        const uint32_t n = e->n;
        const uint32_t w = n & MG_RING_MASK;
        e->ring_z[w][0] = zl;
        e->ring_z[w][1] = zr;
        e->ring_x[w][0] = xl;
        e->ring_x[w][1] = xr;
        const float pk = fmaxf(fabsf(zl), fabsf(zr));
        const float req = pk > e->ceil_s ? e->ceil_s / pk : 1.0f;
        e->ring_req[w] = req;

        const int la = e->la;
        /* sliding minimum of req over [n - la, n] */
        while (e->dq_tail != e->dq_head && e->dq_val[(e->dq_tail - 1) & MG_RING_MASK] >= req) e->dq_tail--;
        e->dq_idx[e->dq_tail & MG_RING_MASK] = n;
        e->dq_val[e->dq_tail & MG_RING_MASK] = req;
        e->dq_tail++;
        while (e->dq_idx[e->dq_head & MG_RING_MASK] + (uint32_t)la < n) e->dq_head++;
        const float hold = e->dq_val[e->dq_head & MG_RING_MASK];

        /* instant down, exponential back up, never above the hold */
        float rel = hold < e->rel_prev ? hold : e->rel_prev + e->a_lim_rel * (hold - e->rel_prev);
        if (hold - rel < 1e-7f) rel = hold;
        e->rel_prev = rel;
        /* moving average over la samples: reaches each peak's required gain
         * exactly when that peak leaves the delay line */
        e->box_sum += (double)rel - (double)e->ring_rel[(n - (uint32_t)la) & MG_RING_MASK];
        e->ring_rel[w] = rel;
        const float b = (float)(e->box_sum / (double)la);
        const float gl = 1.0f + (b - 1.0f) * e->lim_on;
        if (gl < blk_lg) blk_lg = gl;

        /* the delayed audio this gain belongs to */
        float dzl, dzr, dxl, dxr;
        {
            const uint32_t r = (n - (uint32_t)la) & MG_RING_MASK;
            dzl = e->ring_z[r][0];
            dzr = e->ring_z[r][1];
            dxl = e->ring_x[r][0];
            dxr = e->ring_x[r][1];
            if (e->la_xfade > 0) {
                const uint32_t o = (n - (uint32_t)e->la_old) & MG_RING_MASK;
                const float t = (float)e->la_xfade / (float)MG_LA_XFADE;   /* 1 -> 0 */
                dzl += (e->ring_z[o][0] - dzl) * t;
                dzr += (e->ring_z[o][1] - dzr) * t;
                dxl += (e->ring_x[o][0] - dxl) * t;
                dxr += (e->ring_x[o][1] - dxr) * t;
                e->la_xfade--;
            }
        }
        e->n = n + 1;

        float yl = dzl * gl * e->out_s, yr = dzr * gl * e->out_s;
        if (e->lim_on >= 1.0f) {
            /* The lookahead already holds the ceiling; this only catches
             * rounding and the few samples of a lookahead or ceiling change. */
            const float c = e->ceil_s * e->out_s;
            yl = clampf(yl, -c, c);
            yr = clampf(yr, -c, c);
        }
        acc_in += (double)dxl * dxl + (double)dxr * dxr;
        acc_out += (double)yl * yl + (double)yr * yr;

        /* COMPARE: the unprocessed signal, same delay, matched loudness */
        const float cl = dxl * e->match_s, cr = dxr * e->match_s;
        float ol = yl + (cl - yl) * e->cmp, orr = yr + (cr - yr) * e->cmp;
        /* BYPASS: back to the untouched, undelayed input. At wet == 0 this is
         * exactly xl, so the samples come back bit-identical. */
        ol = ol * e->wet + xl * (1.0f - e->wet);
        orr = orr * e->wet + xr * (1.0f - e->wet);

        const float pin = fmaxf(fabsf(xl), fabsf(xr));
        const float pout = fmaxf(fabsf(ol), fabsf(orr));
        if (pin > blk_in) blk_in = pin;
        if (pout > blk_out) blk_out = pout;

        long sl16 = lrintf(ol * 32768.0f), sr16 = lrintf(orr * 32768.0f);
        audio[2 * i] = (int16_t)(sl16 > 32767 ? 32767 : (sl16 < -32768 ? -32768 : sl16));
        audio[2 * i + 1] = (int16_t)(sr16 > 32767 ? 32767 : (sr16 < -32768 ? -32768 : sr16));
    }

    /* the box sum is exact for the neutral case; re-add it now and then so
     * rounding cannot accumulate over hours */
    e->box_refresh += (uint32_t)frames;
    if (e->box_refresh >= 65536u && e->la_xfade == 0) {
        e->box_refresh = 0;
        double sum = 0.0;
        for (uint32_t k = e->n - (uint32_t)e->la; k != e->n; ++k) sum += e->ring_rel[k & MG_RING_MASK];
        e->box_sum = sum;
    }

    /* COMPARE gain: long-term RMS of processed vs dry */
    if (frames > 0) {
        const float k = 1.0f - expf(-(float)frames / (MG_MATCH_TAU * e->fs));
        e->in_ms += k * ((float)(acc_in / frames) - e->in_ms);
        e->out_ms += k * ((float)(acc_out / frames) - e->out_ms);
        if (e->in_ms > 1e-9f && e->out_ms > 1e-12f)
            e->match = clampf(sqrtf(e->out_ms / e->in_ms), 1.0f / 16.0f, 16.0f);
    }

    /* denormal guards */
    for (int c = 0; c < 2; ++c) {
        if (fabsf(e->hpf.z1[c]) < 1e-25f) e->hpf.z1[c] = 0.0f;
        if (fabsf(e->hpf.z2[c]) < 1e-25f) e->hpf.z2[c] = 0.0f;
    }
    if (e->ms < 1e-25f) e->ms = 0.0f;
    if (e->gr_fast < 1e-9f) e->gr_fast = 0.0f;
    if (e->gr_slow < 1e-9f) e->gr_slow = 0.0f;
    if (e->in_ms < 1e-20f) e->in_ms = 0.0f;
    if (e->out_ms < 1e-20f) e->out_ms = 0.0f;

    if (!state_is_finite(e)) {
        e->nan_resets++;
        mg_engine_reset_state(e);
    }

    const float dt = (float)frames / e->fs;
    meter(&e->m_in, &e->h_in, peak_db(blk_in), dt);
    meter(&e->m_out, &e->h_out, peak_db(blk_out), dt);
    meter(&e->m_cgr, &e->h_cgr, blk_cgr, dt);
    meter(&e->m_lgr, &e->h_lgr, blk_lg < 1.0f ? -20.0f * mg_log10(fmaxf(blk_lg, 1e-6f)) : 0.0f, dt);
    if (e->m_cgr < 0.0f) e->m_cgr = 0.0f;
    if (e->m_lgr < 0.0f) e->m_lgr = 0.0f;
}

float mg_meter_in(const mg_engine *e) { return e->m_in; }
float mg_meter_out(const mg_engine *e) { return e->m_out; }
float mg_meter_comp_gr(const mg_engine *e) { return e->m_cgr > 0.0f ? -e->m_cgr : 0.0f; }
float mg_meter_lim_gr(const mg_engine *e) { return e->m_lgr > 0.0f ? -e->m_lgr : 0.0f; }
int mg_latency_samples(const mg_engine *e) { return e->la; }
