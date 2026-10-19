/*
 * MASTER GLUE - CPU benchmark meant to run ON the Move (static aarch64 binary).
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * Runs the engine as the host does - 128-frame int16 blocks at 44.1 kHz - on
 * the synthetic techno mix, and reports the mean and worst block time. It is
 * a normal-priority process: it measures the code, it does not compete with
 * Move's realtime threads (pin it with taskset 0x7).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mg_engine.h"
#include "signals.h"

static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static mg_engine e;

int main(void) {
    const struct { const char *name; float glue, limit; int preset; } cfg[] = {
        {"TECHNO GLUE", 25, 0, 0},
        {"TIGHT TECHNO, +6 dB limit", 40, 6, 2},
    };
    int16_t buf[256];
    for (unsigned c = 0; c < sizeof(cfg) / sizeof(cfg[0]); ++c) {
        mg_engine_init(&e, 44100.0f);
        mg_engine_load_preset(&e, cfg[c].preset);
        mg_engine_set(&e, MG_P_GLUE, cfg[c].glue);
        mg_engine_set(&e, MG_P_LIMIT, cfg[c].limit);
        sig_mix m;
        sig_mix_init(&m, 0.7);
        long t = 0;
        double worst = 0, total = 0;
        const int warm = 400, n = 3445;   /* ~10 s measured */
        for (int i = 0; i < warm + n; ++i) {
            for (int k = 0; k < 128; ++k) {
                double l, r;
                sig_mix_frame(&m, (t++) / 44100.0, &l, &r);
                buf[2 * k] = sig_to_i16(l);
                buf[2 * k + 1] = sig_to_i16(r);
            }
            double t0 = now_us();
            mg_engine_process(&e, buf, 128);
            double dt = now_us() - t0;
            if (i < warm) continue;
            total += dt;
            if (dt > worst) worst = dt;
        }
        printf("%-28s mean %6.1f us (%4.1f %% of 2902 us)  worst %6.1f us\n", cfg[c].name,
               total / n, 100.0 * total / n / 2902.0, worst);
    }
    return 0;
}
