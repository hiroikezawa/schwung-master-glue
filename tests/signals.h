/*
 * MASTER GLUE test signals: deterministic synthetic techno parts.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * These are stand-ins for a real mix, good enough to exercise the dynamics
 * (a 4/4 kick with a low fundamental, an off-beat bass, a 303-like line,
 * hats, a texture), not a judgement of how a real track will sound.
 */
#ifndef MG_SIGNALS_H
#define MG_SIGNALS_H

#include <math.h>
#include <stdint.h>

#define SIG_FS 44100.0
#define SIG_PI 3.14159265358979323846

typedef struct { uint32_t s; } sig_rng;
static inline double sig_noise(sig_rng *r) {
    r->s ^= r->s << 13; r->s ^= r->s >> 17; r->s ^= r->s << 5;
    return (double)r->s / 2147483648.0 - 1.0;
}

/* Beat position helpers at 128 BPM. */
#define SIG_BPM 128.0
static inline double sig_beat_len(void) { return 60.0 / SIG_BPM; }

/* Kick: pitch sweep 160 -> 48 Hz, 300 ms decay, on every beat. */
static inline double sig_kick(double t) {
    double bl = sig_beat_len();
    double ph = fmod(t, bl);
    double f0 = 48.0, f1 = 160.0, k = 30.0;
    /* phase of a sweep f(t) = f0 + (f1-f0) e^{-k t} */
    double phase = 2.0 * SIG_PI * (f0 * ph + (f1 - f0) * (1.0 - exp(-k * ph)) / k);
    double env = exp(-ph / 0.12);
    double click = exp(-ph / 0.002) * 0.3;
    return sin(phase) * env + click * sin(2.0 * SIG_PI * 1800.0 * ph);
}

/* Off-beat bass: 55 Hz with two harmonics, gated between kicks. */
static inline double sig_bass(double t) {
    double bl = sig_beat_len();
    double ph = fmod(t + bl * 0.5, bl);
    double env = ph < bl * 0.4 ? (1.0 - exp(-ph / 0.005)) * exp(-ph / 0.25) : 0.0;
    double w = 2.0 * SIG_PI * 55.0 * t;
    return env * (sin(w) + 0.35 * sin(2.0 * w) + 0.15 * sin(3.0 * w));
}

/* 303-like 16th line: band-limited-ish saw (8 harmonics), accent pattern. */
static inline double sig_acid(double t) {
    static const double notes[16] = {110, 110, 220, 110, 165, 110, 110, 247,
                                     110, 131, 110, 220, 110, 110, 196, 110};
    double sl = sig_beat_len() / 4.0;
    int step = (int)(t / sl) & 15;
    double ph = fmod(t, sl);
    double f = notes[step];
    double env = exp(-ph / ((step & 3) == 2 ? 0.09 : 0.05));
    double s = 0.0;
    for (int h = 1; h <= 8; ++h) s += sin(2.0 * SIG_PI * f * h * t) / h;
    return env * s * 0.6;
}

/* Hats: high-passed noise bursts on the off 8ths, 30 ms. */
static inline double sig_hat(double t, sig_rng *r, double *prev) {
    double sl = sig_beat_len() / 2.0;
    int step = (int)(t / sl);
    double ph = fmod(t, sl);
    double n = sig_noise(r);
    double hp = n - *prev;   /* first difference: a gentle high-pass */
    *prev = n;
    return (step & 1) ? hp * exp(-ph / 0.03) * 0.5 : 0.0;
}

/* Texture: slow-moving wide noise pad, decorrelated L/R. */
static inline double sig_texture(double t, sig_rng *r) {
    return sig_noise(r) * 0.04 * (0.6 + 0.4 * sin(2.0 * SIG_PI * 0.1 * t));
}

typedef struct {
    sig_rng rng_hat, rng_tl, rng_tr;
    double hat_prev;
    double gain;   /* overall level */
} sig_mix;

static inline void sig_mix_init(sig_mix *m, double gain) {
    m->rng_hat.s = 0x1234567u; m->rng_tl.s = 0x89abcdeu; m->rng_tr.s = 0x2468aceu;
    m->hat_prev = 0.0;
    m->gain = gain;
}

/* One stereo sample of the full mix: kick and bass centre, acid slightly
 * left, hats slightly right, texture wide. */
static inline void sig_mix_frame(sig_mix *m, double t, double *l, double *r) {
    double k = sig_kick(t) * 0.9, b = sig_bass(t) * 0.45, a = sig_acid(t) * 0.25;
    double h = sig_hat(t, &m->rng_hat, &m->hat_prev);
    *l = (k + b + a * 1.2 + h * 0.8 + sig_texture(t, &m->rng_tl)) * m->gain;
    *r = (k + b + a * 0.8 + h * 1.2 + sig_texture(t, &m->rng_tr)) * m->gain;
}

static inline int16_t sig_to_i16(double v) {
    long s = lrint(v * 32768.0);
    return (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
}

#endif
