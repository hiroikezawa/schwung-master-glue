/*
 * MASTER GLUE DSP tests. Every test goes through the real plugin entry points
 * (move_audio_fx_init_v2 -> set_param / get_param / process_block) in
 * 128-frame blocks, as the host calls them.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host/audio_fx_api_v2.h"
#include "mg_engine.h"
#include "signals.h"

#define BLOCK 128
#define FS 44100

extern audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } \
    else { failures++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static audio_fx_api_v2_t *api;

typedef struct { void *inst; } fx;

static fx fx_new(void) {
    fx f = {api->create_instance("", NULL)};
    if (!f.inst) { printf("FAIL could not create instance\n"); exit(1); }
    return f;
}
static void fx_free(fx f) { api->destroy_instance(f.inst); }
static void set(fx f, const char *k, const char *v) { api->set_param(f.inst, k, v); }
static const char *get(fx f, const char *k) {
    static char buf[8192];
    int n = api->get_param(f.inst, k, buf, sizeof buf);
    return n < 0 ? NULL : buf;
}
static double getf(fx f, const char *k) { const char *s = get(f, k); return s ? atof(s) : NAN; }

/* Process an interleaved int16 buffer in host-sized blocks. */
static void run(fx f, int16_t *buf, int frames) {
    for (int i = 0; i < frames; i += BLOCK) {
        int n = frames - i < BLOCK ? frames - i : BLOCK;
        api->process_block(f.inst, buf + 2 * i, n);
    }
}

static int16_t *alloc_frames(int frames) { return calloc((size_t)frames * 2, sizeof(int16_t)); }

static void fill_sine(int16_t *b, int frames, double hz, double amp_l, double amp_r, double t0) {
    for (int i = 0; i < frames; ++i) {
        double s = sin(2.0 * SIG_PI * hz * (t0 + i / (double)FS));
        b[2 * i] = sig_to_i16(s * amp_l);
        b[2 * i + 1] = sig_to_i16(s * amp_r);
    }
}

static void fill_mix(int16_t *b, int frames, double gain, double t0) {
    sig_mix m;
    sig_mix_init(&m, gain);
    for (int i = 0; i < frames; ++i) {
        double l, r;
        sig_mix_frame(&m, t0 + i / (double)FS, &l, &r);
        b[2 * i] = sig_to_i16(l);
        b[2 * i + 1] = sig_to_i16(r);
    }
}

static double db(double x) { return 20.0 * log10(x > 1e-12 ? x : 1e-12); }

typedef struct { double peak, rms; } stats;
static stats measure(const int16_t *b, int from, int to) {
    double pk = 0, acc = 0;
    for (int i = from; i < to; ++i) {
        for (int c = 0; c < 2; ++c) {
            double v = b[2 * i + c] / 32768.0;
            if (fabs(v) > pk) pk = fabs(v);
            acc += v * v;
        }
    }
    stats s = {pk, sqrt(acc / (2.0 * (to - from)))};
    return s;
}

static int max_abs_i16(const int16_t *b, int frames) {
    int m = 0;
    for (int i = 0; i < 2 * frames; ++i) { int v = abs(b[i]); if (v > m) m = v; }
    return m;
}

/* Largest sample-to-sample step, a crude click detector. */
static int max_step(const int16_t *b, int frames) {
    int m = 0;
    for (int i = 1; i < frames; ++i)
        for (int c = 0; c < 2; ++c) { int d = abs(b[2 * i + c] - b[2 * (i - 1) + c]); if (d > m) m = d; }
    return m;
}

/* Let the 20 ms parameter smoothing land before measuring: half a second of
 * silence. */
static void settle(fx f) {
    int16_t z[2 * BLOCK];
    for (int i = 0; i < FS / 2; i += BLOCK) { memset(z, 0, sizeof z); api->process_block(f.inst, z, BLOCK); }
}

/* A preset-free, everything-neutral configuration. */
static void neutral(fx f) {
    set(f, "glue", "0");
    set(f, "density", "0");
    set(f, "limit", "0");
    set(f, "output", "0");
    set(f, "ceiling", "-0.3");
}

/* ---------------------------------------------------------------- tests */

static void test_silence(void) {
    fx f = fx_new();
    int n = FS;
    int16_t *b = alloc_frames(n);
    run(f, b, n);
    CHECK(max_abs_i16(b, n) == 0, "silence in -> silence out (default settings)");
    CHECK(getf(f, "comp_gr") == 0.0 && getf(f, "lim_gr") == 0.0, "silence: no gain reduction reported");
    CHECK(getf(f, "in_peak") == -60.0, "silence: input meter at its -60 dB floor");
    free(b);
    fx_free(f);
}

static void test_neutral_is_pure_delay(void) {
    fx f = fx_new();
    neutral(f);
    int n = FS;
    int16_t *in = alloc_frames(n), *b = alloc_frames(n);
    fill_mix(in, n, 0.5, 0.0);
    memcpy(b, in, (size_t)n * 4);
    run(f, b, n);
    int la = (int)getf(f, "latency_samples");
    int diff = 0;
    for (int i = la; i < n; ++i)
        for (int c = 0; c < 2; ++c) if (b[2 * i + c] != in[2 * (i - la) + c]) diff++;
    CHECK(la == 88, "default lookahead = 2 ms = %d samples at 44.1 kHz", la);
    CHECK(diff == 0, "GLUE 0 and nothing over the ceiling: output is the input delayed %d samples, "
          "bit for bit (%d differing samples)", la, diff);
    free(in); free(b);
    fx_free(f);
}

static void test_bypass_bit_exact(void) {
    fx f = fx_new();
    set(f, "glue", "60");
    set(f, "density", "6");
    set(f, "limit", "6");
    int n = 2 * FS;
    int16_t *in = alloc_frames(n), *b = alloc_frames(n);
    fill_mix(in, n, 0.9, 0.0);
    memcpy(b, in, (size_t)n * 4);
    run(f, b, FS);                  /* processing for a second */
    set(f, "bypass", "ON");
    run(f, b + 2 * FS, FS);         /* then bypassed */
    int skip = FS + FS / 50;        /* after the 10 ms cross-fade */
    int diff = 0;
    for (int i = skip; i < n; ++i)
        for (int c = 0; c < 2; ++c) if (b[2 * i + c] != in[2 * i + c]) diff++;
    CHECK(diff == 0, "BYPASS ON: bit-identical to the input after the cross-fade (%d differing)", diff);
    CHECK(strcmp(get(f, "bypass"), "ON") == 0, "bypass reads back as ON");
    free(in); free(b);
    fx_free(f);
}

/* Steady-state static curve: a 1 kHz sine, HPF off, hard knee. */
static double steady_out_db(const char *ratio, double in_rms_db, const char *knee) {
    fx f = fx_new();
    set(f, "ratio", ratio);
    set(f, "threshold", "-24");
    set(f, "knee", knee);
    set(f, "low_protect", "OFF");
    set(f, "release", "1000ms");
    set(f, "punch", "1");
    set(f, "ceiling", "-0.3");
    settle(f);
    int n = 3 * FS;
    int16_t *b = alloc_frames(n);
    double amp = pow(10.0, in_rms_db / 20.0) * sqrt(2.0);
    fill_sine(b, n, 1000.0, amp, amp, 0.0);
    run(f, b, n);
    stats s = measure(b, 2 * FS, n);
    free(b);
    fx_free(f);
    return db(s.rms);
}

static void test_ratio(void) {
    const char *names[4] = {"1.5:1", "2:1", "3:1", "4:1"};
    const double r[4] = {1.5, 2.0, 3.0, 4.0};
    for (int i = 0; i < 4; ++i) {
        double in_db = -12.0;   /* 12 dB over a -24 dB threshold */
        double want = -24.0 + 12.0 / r[i];
        double got = steady_out_db(names[i], in_db, "0");
        CHECK(fabs(got - want) < 0.35, "ratio %s: -12 dB RMS in, threshold -24 -> %.2f dB out (expect %.2f)",
              names[i], got, want);
    }
    double below = steady_out_db("4:1", -30.0, "0");
    CHECK(fabs(below + 30.0) < 0.05, "below threshold the level is untouched (%.2f dB for -30)", below);
    /* soft knee: at the threshold, half the knee's worth of reduction */
    double at = steady_out_db("2:1", -24.0, "6");
    double want = -24.0 - (0.5 * 3.0 * 3.0 / 12.0);   /* (1-1/R) (w/2)^2 / (2w) */
    CHECK(fabs(at - want) < 0.2, "6 dB soft knee at the threshold: %.2f dB (expect %.2f)", at, want);
}

/* Gain reduction over time for a step of loud signal, read from the meters'
 * source: the output RMS envelope relative to the input. */
static double time_to_gr_fraction(const char *attack, double frac) {
    fx f = fx_new();
    set(f, "threshold", "-30");
    set(f, "ratio", "4:1");
    set(f, "knee", "0");
    set(f, "low_protect", "OFF");
    set(f, "punch", attack);
    set(f, "release", "1000ms");
    settle(f);
    int n = FS;
    int16_t *b = alloc_frames(n);
    double amp = pow(10.0, -10.0 / 20.0) * sqrt(2.0);
    fill_sine(b, n, 1000.0, amp, amp, 0.0);
    run(f, b, n);
    int la = (int)getf(f, "latency_samples");
    /* final GR from the last 100 ms, then the time the envelope crosses frac */
    stats fin = measure(b, n - FS / 10, n);
    double final_gr = -10.0 - db(fin.rms);
    double t = -1;
    int w = 44;   /* 1 ms windows */
    for (int i = la; i + w < n; i += w) {
        stats s = measure(b, i, i + w);
        double gr = -10.0 - db(s.rms);
        if (gr >= frac * final_gr) { t = (i - la) / (double)FS * 1000.0; break; }
    }
    free(b);
    fx_free(f);
    return t;
}

static void test_attack(void) {
    double t1 = time_to_gr_fraction("1", 0.63);
    double t10 = time_to_gr_fraction("10", 0.63);
    double t30 = time_to_gr_fraction("30", 0.63);
    double t50 = time_to_gr_fraction("50", 0.63);
    CHECK(t1 < t10 && t10 < t30 && t30 < t50, "PUNCH orders the attack: 63%% of GR at %.1f / %.1f / %.1f / %.1f ms "
          "for 1 / 10 / 30 / 50 ms", t1, t10, t30, t50);
    CHECK(t30 > 15.0 && t30 < 45.0, "PUNCH 30 ms reaches 63%% of its reduction in %.1f ms", t30);
}

/* Recovery after a burst of `burst_s` seconds: ms until GR < 10% of its peak. */
static double recovery_ms(const char *release, double burst_s) {
    fx f = fx_new();
    set(f, "threshold", "-30");
    set(f, "ratio", "4:1");
    set(f, "knee", "0");
    set(f, "low_protect", "OFF");
    set(f, "punch", "5");
    set(f, "release", release);
    settle(f);
    int burst = (int)(burst_s * FS), n = burst + 3 * FS;
    int16_t *b = alloc_frames(n);
    double loud = pow(10.0, -10.0 / 20.0) * sqrt(2.0), quiet = pow(10.0, -40.0 / 20.0) * sqrt(2.0);
    fill_sine(b, burst, 1000.0, loud, loud, 0.0);
    fill_sine(b + 2 * burst, n - burst, 1000.0, quiet, quiet, burst_s);
    /* follow the compressor's own GR meter block by block */
    double peak = 0, t = -1;
    for (int i = 0; i < n; i += BLOCK) {
        api->process_block(f.inst, b + 2 * i, n - i < BLOCK ? n - i : BLOCK);
        mg_engine *e = (mg_engine *)f.inst;
        double gr = e->auto_release ? fmax(e->gr_fast, e->gr_slow) : e->gr_fast;
        if (i < burst) { if (gr > peak) peak = gr; }
        else if (gr < 0.1 * peak) { t = (i - burst) / (double)FS * 1000.0; break; }
    }
    free(b);
    fx_free(f);
    return t;
}

static void test_release(void) {
    double r100 = recovery_ms("100ms", 0.5), r600 = recovery_ms("600ms", 0.5);
    CHECK(r100 > 0 && r100 < r600, "RELEASE 100 ms recovers faster than 600 ms (%.0f vs %.0f ms)", r100, r600);
    double a_short = recovery_ms("AUTO", 0.05), a_long = recovery_ms("AUTO", 3.0);
    CHECK(a_short > 0 && a_long > 2.0 * a_short,
          "AUTO is program dependent: recovers in %.0f ms after a 50 ms hit, %.0f ms after 3 s of reduction",
          a_short, a_long);
}

static double gr_for_low_tone(const char *hpf) {
    fx f = fx_new();
    set(f, "threshold", "-30");
    set(f, "low_protect", hpf);
    settle(f);
    int n = 2 * FS;
    int16_t *b = alloc_frames(n);
    double amp = pow(10.0, -10.0 / 20.0) * sqrt(2.0);
    fill_sine(b, n, 45.0, amp, amp, 0.0);
    run(f, b, n);
    mg_engine *e = (mg_engine *)f.inst;
    double gr = fmax(e->gr_fast, e->gr_slow);
    free(b);
    fx_free(f);
    return gr;
}

static void test_sidechain_hpf(void) {
    double off = gr_for_low_tone("OFF"), h100 = gr_for_low_tone("100Hz"), h200 = gr_for_low_tone("200Hz");
    CHECK(off > h100 && h100 > h200, "LOW PROTECT reduces GR from a 45 Hz tone: OFF %.1f, 100 Hz %.1f, 200 Hz %.1f dB",
          off, h100, h200);
    /* ...and only the detector is filtered: with no compression the 45 Hz
     * tone comes out exactly as it went in (see the pure-delay test), and
     * with compression its level drops by the GR alone, not by a filter. */
    fx f = fx_new();
    set(f, "glue", "0");
    set(f, "low_protect", "200Hz");
    int n = FS;
    int16_t *b = alloc_frames(n);
    fill_sine(b, n, 45.0, 0.3, 0.3, 0.0);
    run(f, b, n);
    stats s = measure(b, FS / 2, n);
    CHECK(fabs(db(s.rms) - db(0.3 / sqrt(2.0))) < 0.01, "LOW PROTECT 200 Hz does not touch the 45 Hz audio (%.3f dB)",
          db(s.rms) - db(0.3 / sqrt(2.0)));
    free(b);
    fx_free(f);
}

static void test_stereo_link(void) {
    fx f = fx_new();
    set(f, "threshold", "-30");
    set(f, "ratio", "4:1");
    set(f, "low_protect", "OFF");
    int n = 2 * FS;
    int16_t *in = alloc_frames(n), *b = alloc_frames(n);
    fill_sine(in, n, 500.0, 0.6, 0.06, 0.0);   /* left 20 dB louder */
    memcpy(b, in, (size_t)n * 4);
    run(f, b, n);
    int la = (int)getf(f, "latency_samples");
    stats si = measure(in, FS - la, n - la), so = measure(b, FS, n);
    (void)si;
    double gl = 0, gr_ = 0, il = 0, ir = 0;
    for (int i = FS; i < n; ++i) {
        gl += fabs((double)b[2 * i]); gr_ += fabs((double)b[2 * i + 1]);
        il += fabs((double)in[2 * (i - la)]); ir += fabs((double)in[2 * (i - la) + 1]);
    }
    double dl = db(gl / il), dr = db(gr_ / ir);
    CHECK(dl < -3.0 && fabs(dl - dr) < 0.05, "stereo link: L and R get the same reduction (%.2f / %.2f dB), "
          "so the image does not move", dl, dr);
    (void)so;
    free(in); free(b);
    fx_free(f);
}

static void test_ceiling(void) {
    const char *ceils[3] = {"-0.3", "-1", "-6"};
    for (int k = 0; k < 3; ++k) {
        fx f = fx_new();
        set(f, "ceiling", ceils[k]);
        set(f, "limit", "12");
        set(f, "density", "12");
        set(f, "glue", "0");
        settle(f);
        int n = 3 * FS;
        int16_t *b = alloc_frames(n);
        fill_mix(b, n, 1.0, 0.0);     /* clipped, full-scale material + 24 dB */
        run(f, b, n);
        int lim = (int)ceil(pow(10.0, atof(ceils[k]) / 20.0) * 32768.0);
        int got = max_abs_i16(b, n);
        CHECK(got <= lim, "ceiling %s dBFS held with +24 dB into the limiter: max %d <= %d", ceils[k], got, lim);
        fx_free(f);
        free(b);
    }
    /* impulse */
    fx f = fx_new();
    set(f, "limit", "12");
    int n = FS;
    int16_t *b = alloc_frames(n);
    b[2 * 1000] = 32767; b[2 * 1000 + 1] = -32768;
    run(f, b, n);
    int lim = (int)ceil(pow(10.0, -1.0 / 20.0) * 32768.0);
    CHECK(max_abs_i16(b, n) <= lim, "a single full-scale impulse +12 dB stays under -1 dBFS (%d <= %d)",
          max_abs_i16(b, n), lim);
    free(b);
    fx_free(f);
}

static void test_limiter_idle_at_default(void) {
    /* Default settings on a mix peaking at about -3 dBFS: the limiter should
     * have essentially nothing to do. */
    fx f = fx_new();
    int n = 8 * FS;
    int16_t *b = alloc_frames(n);
    fill_mix(b, n, 0.55, 0.0);
    stats in = measure(b, 0, n);
    run(f, b, n);
    mg_engine *e = (mg_engine *)f.inst;
    (void)e;
    double lim_db = getf(f, "lim_gr");
    printf("     (mix input peak %.1f dBFS)\n", db(in.peak));
    CHECK(lim_db > -0.5, "defaults on a -3 dBFS mix: limiter GR at the end %.2f dB (expected ~0)", lim_db);
    free(b);
    fx_free(f);
}

static void test_param_jumps_no_clicks(void) {
    /* A quiet sine through big parameter jumps: the largest sample step must
     * stay close to the sine's own. */
    fx f = fx_new();
    int n = 4 * FS;
    int16_t *b = alloc_frames(n);
    double amp = 0.25;
    fill_sine(b, n, 200.0, amp, amp, 0.0);
    int natural = (int)ceil(amp * 32768.0 * 2.0 * SIG_PI * 200.0 / FS) + 2;
    const char *seq[][2] = {{"density", "12"}, {"glue", "100"}, {"ratio", "4:1"}, {"release", "50ms"},
                            {"low_protect", "OFF"}, {"lookahead", "1"}, {"lookahead", "3"},
                            {"comp_bypass", "ON"}, {"lim_bypass", "ON"}, {"compare", "ON"},
                            {"compare", "OFF"}, {"output", "-24"}, {"output", "0"}, {"ceiling", "-6"},
                            {"density", "0"}, {"glue", "0"}, {"bypass", "ON"}, {"bypass", "OFF"}};
    int segs = (int)(sizeof seq / sizeof seq[0]);
    int per = n / segs;
    per -= per % BLOCK;
    for (int k = 0; k < segs; ++k) {
        set(f, seq[k][0], seq[k][1]);
        run(f, b + 2 * per * k, per);
    }
    /* A click is a sample step far larger than the waveform's own slope at
     * its current size. Judge each segment (which starts at the change)
     * against the largest amplitude around it. */
    double w = 2.0 * SIG_PI * 200.0 / FS, worst = 0;
    int worst_k = -1;
    for (int k = 0; k < segs; ++k) {
        int from = per * k, to = per * (k + 1);
        int pk = 0;
        for (int i = (from > per ? from - per : 0); i < to; ++i)
            for (int c = 0; c < 2; ++c) if (abs(b[2 * i + c]) > pk) pk = abs(b[2 * i + c]);
        int st = max_step(b + 2 * (from > 0 ? from - 1 : 0), to - (from > 0 ? from - 1 : 0));
        double ratio = st / (pk * w + 2.0);
        if (ratio > worst) { worst = ratio; worst_k = k; }
    }
    (void)natural;
    CHECK(worst < 1.3, "%d abrupt parameter changes: worst sample step is %.2fx the sine's own slope "
          "(after '%s %s'; a click would be many times it)", segs, worst, seq[worst_k][0], seq[worst_k][1]);
    free(b);
    fx_free(f);
}

static void test_presets_and_state(void) {
    fx f = fx_new();
    CHECK(getf(f, "preset_count") == 3, "3 factory presets");
    const char *names[3] = {"TECHNO GLUE", "DEEP GLUE", "TIGHT TECHNO"};
    const char *ratio[3] = {"2:1", "1.5:1", "3:1"};
    const char *punch[3] = {"30", "40", "15"};
    const char *rel[3] = {"AUTO", "AUTO", "100ms"};
    const char *hpf[3] = {"100Hz", "120Hz", "90Hz"};
    for (int i = 0; i < 3; ++i) {
        char idx[16];
        snprintf(idx, sizeof idx, "%d", i);
        set(f, "glue", "77");
        set(f, "preset", idx);
        int ok = !strcmp(get(f, "preset_name"), names[i]) && !strcmp(get(f, "ratio"), ratio[i]) &&
                 !strcmp(get(f, "punch"), punch[i]) && !strcmp(get(f, "release"), rel[i]) &&
                 !strcmp(get(f, "low_protect"), hpf[i]) && !strcmp(get(f, "ceiling"), "-1") &&
                 !strcmp(get(f, "knee"), "6") && getf(f, "glue") == 25.0;
        char desc[128], r_[16], p_[16], rl[16], h_[16];
        snprintf(r_, sizeof r_, "%.15s", get(f, "ratio")); snprintf(p_, sizeof p_, "%.15s", get(f, "punch"));
        snprintf(rl, sizeof rl, "%.15s", get(f, "release")); snprintf(h_, sizeof h_, "%.15s", get(f, "low_protect"));
        snprintf(desc, sizeof desc, "ratio %s attack %s ms release %s HPF %s", r_, p_, rl, h_);
        CHECK(ok, "preset %d %s: %s, knee 6, ceiling -1, GLUE 25%%", i, names[i], desc);
    }
    CHECK(!strcmp(get(f, "preset_name"), "TIGHT TECHNO"), "default state names the loaded preset");

    /* GLUE and THRESHOLD are one value */
    set(f, "threshold", "-32");
    CHECK(fabs(getf(f, "glue") - 40.0) < 1e-3, "THRESHOLD -32 dB moves GLUE to 40%% (%s)", get(f, "glue"));
    set(f, "glue", "25");
    CHECK(fabs(getf(f, "threshold") + 20.0) < 1e-3, "GLUE 25%% reads back as THRESHOLD -20 dB (%s)", get(f, "threshold"));

    /* state round trip into a fresh instance */
    set(f, "glue", "37"); set(f, "punch", "12"); set(f, "release", "300ms"); set(f, "low_protect", "OFF");
    set(f, "density", "2.5"); set(f, "limit", "1.5"); set(f, "ceiling", "-0.5"); set(f, "output", "-3");
    set(f, "ratio", "4:1"); set(f, "knee", "3"); set(f, "lim_release", "250"); set(f, "lookahead", "1.5");
    set(f, "comp_bypass", "ON"); set(f, "lim_bypass", "ON"); set(f, "compare", "ON");
    char state[2048];
    snprintf(state, sizeof state, "%s", get(f, "state"));
    fx g = fx_new();
    set(g, "state", state);
    const char *keys[] = {"glue", "punch", "release", "low_protect", "density", "limit", "ceiling", "output",
                          "ratio", "threshold", "knee", "lim_release", "lookahead", "comp_bypass",
                          "lim_bypass", "bypass", "compare", "preset"};
    int same = 1;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
        char a[128];
        snprintf(a, sizeof a, "%.100s", get(f, keys[i]));
        if (strcmp(a, get(g, keys[i]))) { same = 0; printf("     %s: %s vs %s\n", keys[i], a, get(g, keys[i])); }
    }
    CHECK(same, "state round trip restores every parameter: %s", state);
    set(g, "state", "{\"glue\":");
    CHECK(getf(g, "glue") == 37.0, "a malformed state changes nothing");
    set(g, "glue", "abc");
    set(g, "ratio", "9:1");
    CHECK(getf(g, "glue") == 37.0 && !strcmp(get(g, "ratio"), "4:1"), "garbage values are ignored");
    set(g, "glue", "1000");
    CHECK(getf(g, "glue") == 100.0, "out-of-range values are clamped");
    set(g, "ratio", "1");
    CHECK(!strcmp(get(g, "ratio"), "2:1"), "an enum also takes its index (\"1\" -> 2:1)");
    fx_free(f);
    fx_free(g);
}

static void test_stress(void) {
    /* Random full-scale noise and random parameter writes every block for a
     * simulated 10 minutes: no NaN reset, ceiling held whenever the limiter
     * has been on for a while. */
    fx f = fx_new();
    sig_rng r = {0xdeadbeefu};
    int16_t b[2 * BLOCK];
    const char *keys[] = {"glue", "punch", "release", "low_protect", "density", "limit", "ratio", "knee",
                          "lim_release", "lookahead", "comp_bypass", "compare", "output"};
    const double lo[] = {0, 1, 0, 0, 0, 0, 0, 0, 20, 1, 0, 0, -24};
    const double hi[] = {100, 50, 9, 8, 12, 12, 3, 12, 500, 3, 1, 1, 0};
    long blocks = 10L * 60 * FS / BLOCK;
    int over = 0, ceil_lim = (int)ceil(pow(10.0, -1.0 / 20.0) * 32768.0);
    long quiet_blocks = 0;
    for (long k = 0; k < blocks; ++k) {
        double level = (k / 3000) % 3 == 0 ? 1.0 : ((k / 3000) % 3 == 1 ? 0.05 : 0.0);
        for (int i = 0; i < 2 * BLOCK; ++i) b[i] = sig_to_i16(sig_noise(&r) * level * 1.2);
        if (k % 7 == 0) {
            int which = (int)((sig_noise(&r) * 0.5 + 0.5) * 12.999);
            double v = lo[which] + (hi[which] - lo[which]) * (sig_noise(&r) * 0.5 + 0.5);
            char s[32];
            snprintf(s, sizeof s, "%.3f", v);
            set(f, keys[which], s);
        }
        api->process_block(f.inst, b, BLOCK);
        mg_engine *e = (mg_engine *)f.inst;
        /* the ceiling is a promise of the limiter path; COMPARE plays the
         * dry signal, so only check while it is off and settled */
        if (e->cmp == 0.0f && e->lim_on == 1.0f && e->la_xfade == 0) {
            if (max_abs_i16(b, BLOCK) > ceil_lim) over++;
        }
        if (level == 0.0 && max_abs_i16(b, BLOCK) == 0) quiet_blocks++;
    }
    mg_engine *e = (mg_engine *)f.inst;
    CHECK(e->nan_resets == 0, "10 min of full-scale noise and random parameter writes: %d NaN resets", e->nan_resets);
    CHECK(over == 0, "...ceiling -1 dBFS never exceeded on the limiter path (%d blocks over)", over);
    CHECK(quiet_blocks > 0, "...and silence after it decays to exact zero (%ld silent blocks)", quiet_blocks);
    fx_free(f);
}

static void test_compare(void) {
    fx f = fx_new();
    set(f, "glue", "60");
    set(f, "density", "6");
    int n = 12 * FS;
    int16_t *b = alloc_frames(n), *c = alloc_frames(n);
    fill_mix(b, n, 0.5, 0.0);
    memcpy(c, b, (size_t)n * 4);
    run(f, b, n);
    stats proc = measure(b, n - 4 * FS, n);
    fx g = fx_new();
    set(g, "glue", "60");
    set(g, "density", "6");
    run(g, c, 8 * FS);
    set(g, "compare", "ON");
    run(g, c + 2 * 8 * FS, n - 8 * FS);
    stats cmp = measure(c, n - 4 * FS, n);
    CHECK(fabs(db(cmp.rms) - db(proc.rms)) < 0.5, "COMPARE plays the dry signal at the processed loudness: "
          "%.2f vs %.2f dB RMS", db(cmp.rms), db(proc.rms));
    free(b); free(c);
    fx_free(f); fx_free(g);
}

/* A / B / C comparison on the synthetic mix, with the numbers the README
 * reports. B and C are level-matched to A by RMS for the listening files. */
static void compare_conditions(void) {
    printf("\n== A/B/C comparison, synthetic techno mix, 16 s, defaults (TECHNO GLUE)\n");
    const double gains[2] = {0.55, 0.9};
    for (int gi = 0; gi < 2; ++gi) {
        int n = 16 * FS;
        int16_t *a = alloc_frames(n), *bb = alloc_frames(n), *cc = alloc_frames(n);
        fill_mix(a, n, gains[gi], 0.0);
        memcpy(bb, a, (size_t)n * 4);
        memcpy(cc, a, (size_t)n * 4);
        fx fb = fx_new(), fc = fx_new();
        set(fb, "lim_bypass", "ON");
        double max_cgr = 0, sum_cgr = 0, max_lgr = 0;
        int blocks = 0;
        for (int i = 0; i < n; i += BLOCK) {
            int nb = n - i < BLOCK ? n - i : BLOCK;
            api->process_block(fb.inst, bb + 2 * i, nb);
            api->process_block(fc.inst, cc + 2 * i, nb);
            if (i > 4 * FS) {
                mg_engine *e = (mg_engine *)fc.inst;
                double g = e->auto_release ? fmax(e->gr_fast, e->gr_slow) : e->gr_fast;
                sum_cgr += g; blocks++;
                if (g > max_cgr) max_cgr = g;
                double l = -getf(fc, "lim_gr");
                if (l > max_lgr) max_lgr = l;
            }
        }
        stats sa = measure(a, 4 * FS, n), sb = measure(bb, 4 * FS, n), sc = measure(cc, 4 * FS, n);
        printf("  input level x%.2f\n", gains[gi]);
        printf("  %-22s peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", "A bypass", db(sa.peak), db(sa.rms), db(sa.peak / sa.rms));
        printf("  %-22s peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", "B compressor only", db(sb.peak), db(sb.rms), db(sb.peak / sb.rms));
        printf("  %-22s peak %6.2f dBFS  RMS %6.2f dBFS  crest %5.2f dB\n", "C compressor+limiter", db(sc.peak), db(sc.rms), db(sc.peak / sc.rms));
        printf("  comp GR mean %.2f dB, max %.2f dB; limiter GR max (meter) %.2f dB\n",
               sum_cgr / blocks, max_cgr, max_lgr);
        fx_free(fb); fx_free(fc);
        free(a); free(bb); free(cc);
    }
}

static void bench(void) {
    fx f = fx_new();
    int n = 60 * FS;
    int16_t *b = alloc_frames(n);
    fill_mix(b, n, 0.7, 0.0);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    run(f, b, n);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    printf("\n== speed (this machine, not the Move): 60 s of audio in %.3f s = %.2f %% of real time, "
           "%.1f us per 128-frame block\n", s, s / 60.0 * 100.0, s / (n / (double)BLOCK) * 1e6);
    free(b);
    fx_free(f);
}

int main(void) {
    host_api_v1_t host;
    memset(&host, 0, sizeof host);
    host.api_version = 1;
    host.sample_rate = FS;
    host.frames_per_block = BLOCK;
    api = move_audio_fx_init_v2(&host);
    if (!api) { printf("FAIL init\n"); return 1; }

    test_silence();
    test_neutral_is_pure_delay();
    test_bypass_bit_exact();
    test_ratio();
    test_attack();
    test_release();
    test_sidechain_hpf();
    test_stereo_link();
    test_ceiling();
    test_limiter_idle_at_default();
    test_param_jumps_no_clicks();
    test_presets_and_state();
    test_compare();
    test_stress();
    compare_conditions();
    bench();
    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("\nALL ENGINE TESTS PASSED\n");
    return 0;
}
