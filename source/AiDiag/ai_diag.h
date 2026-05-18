/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * ai_diag.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Shared types, constants, and data structures for the AI-driven diagnostic
 * service. This header is the single source of truth for all cross-module types.
 *
 * The AiDiag service watches the anomaly_results.csv produced by the anomaly
 * detection application and, for every non-Normal event, drives an iterative
 * LLM-assisted diagnostic workflow:
 *
 *   anomaly event → LLM → next_commands → execute → LLM → ... → analysis_complete
 */

#ifndef AI_DIAG_H
#define AI_DIAG_H

#include <stddef.h>
#include <time.h>

/* ── Version ──────────────────────────────────────────────────────────────── */
#define AI_DIAG_VERSION             "1.0.0"

/* ── Hard limits (tune for embedded memory budget) ────────────────────────── */
#define AI_DIAG_MAX_TURNS           5       /* max LLM round-trips per event   */
#define AI_DIAG_MAX_COMMANDS        16      /* max commands per LLM response   */
#define AI_DIAG_MAX_CMD_LEN         512     /* max bytes for one command string */
#define AI_DIAG_MAX_CMD_OUTPUT      8192    /* max bytes captured per command  */
#define AI_DIAG_MAX_LLM_RESP        65536   /* max LLM HTTP response bytes     */
#define AI_DIAG_MAX_ACTIONS         16      /* max recommended actions         */
#define AI_DIAG_MAC_LEN             32      /* MAC address string length       */
#define AI_DIAG_MAX_TRACKED_MACS    32      /* per-MAC cooldown table size     */

/* ── Anomaly types (mirrors anomaly_type column in anomaly_results.csv) ───── */
typedef enum {
    AI_ANOMALY_CPU     = 0,    /* "CPU"    — only CPU model flagged            */
    AI_ANOMALY_MEMORY  = 1,    /* "Memory" — only memory model flagged         */
    AI_ANOMALY_BOTH    = 2,    /* "Both"   — both models flagged               */
    AI_ANOMALY_UNKNOWN = 3     /* parse failure / unrecognised string          */
} AiAnomalyType;

/* ── One anomaly event parsed from anomaly_results.csv ───────────────────── */
typedef struct {
    char          timestamp[64];
    char          mac[AI_DIAG_MAC_LEN];
    AiAnomalyType anomaly_type;
    float         cpu_mse;
    float         mem_mse;
    float         cpu_sev;         /* MSE / threshold ratio                   */
    float         mem_sev;
    int           cpu_flag;        /* 1 = CPU anomaly flagged                 */
    int           mem_flag;        /* 1 = memory anomaly flagged              */
} AnomalyEvent;

/* ── LLM response status values ──────────────────────────────────────────── */
typedef enum {
    LLM_STATUS_NEED_DATA         = 0,   /* more diagnostic data required      */
    LLM_STATUS_ANALYSIS_COMPLETE = 1,   /* root cause identified              */
    LLM_STATUS_ERROR             = 2    /* parse failure or transport error   */
} LlmStatus;

/* ── Parsed diagnostic response from the LLM ─────────────────────────────── */
typedef struct {
    LlmStatus status;

    /* ── need_data fields ── */
    char next_commands[AI_DIAG_MAX_COMMANDS][AI_DIAG_MAX_CMD_LEN];
    int  num_commands;

    /* ── analysis_complete fields ── */
    char suspected_issue[256];
    char confidence[16];                /* "high" | "medium" | "low"          */
    char recommended_action[AI_DIAG_MAX_ACTIONS][512];
    int  num_actions;
} LlmResponse;

/* ── Anomaly type → human-readable label ─────────────────────────────────── */
static inline const char *ai_anomaly_label(AiAnomalyType t)
{
    switch (t) {
        case AI_ANOMALY_CPU:    return "cpu_anomaly";
        case AI_ANOMALY_MEMORY: return "memory_anomaly";
        case AI_ANOMALY_BOTH:   return "cpu_memory_anomaly";
        default:                return "unknown_anomaly";
    }
}

#endif /* AI_DIAG_H */
