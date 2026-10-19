/*
 * MASTER GLUE - parameter table.
 *
 * Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
 *
 * The order here is the order of chain_params in module.json, and
 * scripts/metadata.py fails the build if the two disagree on any key, range
 * or default. Enum parameters are stored as their option index.
 */
#ifndef MG_PARAMS_H
#define MG_PARAMS_H

enum {
    MG_P_GLUE,        /* 0..100 %, the threshold macro (see mg_glue_to_threshold) */
    MG_P_PUNCH,       /* attack, ms */
    MG_P_RELEASE,     /* enum: AUTO or a fixed time (MG_RELEASE_MS) */
    MG_P_LOW_PROTECT, /* enum: sidechain HPF (MG_HPF_HZ) */
    MG_P_DENSITY,     /* makeup gain, dB */
    MG_P_LIMIT,       /* gain into the limiter, dB */
    MG_P_CEILING,     /* limiter ceiling, dBFS */
    MG_P_OUTPUT,      /* final gain, dB (attenuation only) */
    MG_P_RATIO,       /* enum (MG_RATIO) */
    MG_P_THRESHOLD,   /* dB: a second VIEW of GLUE, not a separate value */
    MG_P_KNEE,        /* dB */
    MG_P_LIM_RELEASE, /* ms */
    MG_P_LOOKAHEAD,   /* ms */
    MG_P_COMP_BYPASS, /* enum OFF/ON */
    MG_P_LIM_BYPASS,  /* enum OFF/ON */
    MG_P_BYPASS,      /* enum OFF/ON */
    MG_P_COMPARE,     /* enum OFF/ON: level-matched bypass for listening */
    MG_P_COUNT
};

typedef struct {
    const char *key;
    float min, max, def;
    int is_enum;
} mg_param_info;

static const mg_param_info MG_PARAMS[MG_P_COUNT] = {
    {"glue", 0.0f, 100.0f, 25.0f, 0},
    {"punch", 1.0f, 50.0f, 30.0f, 0},
    {"release", 0.0f, 9.0f, 0.0f, 1},
    {"low_protect", 0.0f, 8.0f, 5.0f, 1},
    {"density", 0.0f, 12.0f, 0.0f, 0},
    {"limit", 0.0f, 12.0f, 0.0f, 0},
    {"ceiling", -6.0f, -0.3f, -1.0f, 0},
    {"output", -24.0f, 0.0f, 0.0f, 0},
    {"ratio", 0.0f, 3.0f, 1.0f, 1},
    {"threshold", -80.0f, 0.0f, -20.0f, 0},
    {"knee", 0.0f, 12.0f, 6.0f, 0},
    {"lim_release", 20.0f, 500.0f, 100.0f, 0},
    {"lookahead", 1.0f, 3.0f, 2.0f, 0},
    {"comp_bypass", 0.0f, 1.0f, 0.0f, 1},
    {"lim_bypass", 0.0f, 1.0f, 0.0f, 1},
    {"bypass", 0.0f, 1.0f, 0.0f, 1},
    {"compare", 0.0f, 1.0f, 0.0f, 1},
};

/* Option tables. Index 0 of RELEASE is AUTO, of LOW_PROTECT is OFF. */
#define MG_RELEASE_COUNT 10
static const char *const MG_RELEASE_NAMES[MG_RELEASE_COUNT] = {
    "AUTO", "50ms", "100ms", "150ms", "200ms", "300ms", "400ms", "600ms", "800ms", "1000ms"};
static const float MG_RELEASE_MS[MG_RELEASE_COUNT] = {
    0.0f, 50.0f, 100.0f, 150.0f, 200.0f, 300.0f, 400.0f, 600.0f, 800.0f, 1000.0f};

#define MG_HPF_COUNT 9
static const char *const MG_HPF_NAMES[MG_HPF_COUNT] = {
    "OFF", "40Hz", "60Hz", "80Hz", "90Hz", "100Hz", "120Hz", "150Hz", "200Hz"};
static const float MG_HPF_HZ[MG_HPF_COUNT] = {
    0.0f, 40.0f, 60.0f, 80.0f, 90.0f, 100.0f, 120.0f, 150.0f, 200.0f};

#define MG_RATIO_COUNT 4
static const char *const MG_RATIO_NAMES[MG_RATIO_COUNT] = {"1.5:1", "2:1", "3:1", "4:1"};
static const float MG_RATIO[MG_RATIO_COUNT] = {1.5f, 2.0f, 3.0f, 4.0f};

static const char *const MG_SWITCH_NAMES[2] = {"OFF", "ON"};

/* GLUE and THRESHOLD are one value in two units: threshold = -0.8 dB per
 * GLUE percent, so 0 % = 0 dBFS (= compressor off, see mg_engine.c),
 * 25 % = -20 dBFS, 40 % = -32 dBFS, 100 % = -80 dBFS. Writing either moves
 * both. -20 dBFS is where a mix peaking near -2 dBFS (RMS about -16 dBFS)
 * gets the 1-3 dB a bus compressor is meant for, measured in
 * tests/test_engine.c. */
#define MG_GLUE_DB_PER_PERCENT 0.8f
static inline float mg_glue_to_threshold(float glue) { return -MG_GLUE_DB_PER_PERCENT * glue; }
static inline float mg_threshold_to_glue(float thr) { return -thr / MG_GLUE_DB_PER_PERCENT; }

#endif
