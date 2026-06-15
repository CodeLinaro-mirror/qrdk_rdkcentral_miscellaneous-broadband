/*
 * If not stated otherwise in this file or this component's Licenses.txt file
 * the following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
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
 * anomaly_service_csv.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Reader for /rdklogs/logs/anomaly_results.csv produced by anomaly_app.
 *
 * Output CSV schema written by anomaly_app_main.cc WriteResultRow():
 *   timestamp, CMMAC,
 *   dense_cpu_mse, dense_mem_mse,
 *   dense_cpu_flag, dense_mem_flag,
 *   dense_cpu_sev,  dense_mem_sev,     <- float ratio: MSE / threshold
 *   anomaly_type                        <- Normal | CPU | Memory | Both
 *
 * Severity strings exposed by this API: "normal" | "low" | "medium" | "high"
 */

#ifndef ANOMALY_SERVICE_CSV_H
#define ANOMALY_SERVICE_CSV_H

#include <stddef.h>

/* ── Default file paths ──────────────────────────────────────────────────── */
#define ANOMALY_RESULT_CSV    "/rdklogs/logs/anomaly_results.csv"
#define ANOMALY_TELEMETRY_CSV "/rdklogs/logs/system_stats_data.csv"

/* ── Severity rank constants used for min_severity filtering ─────────────── */
#define ANOMALY_SEV_RANK_NORMAL  0
#define ANOMALY_SEV_RANK_LOW     1
#define ANOMALY_SEV_RANK_MEDIUM  2
#define ANOMALY_SEV_RANK_HIGH    3

/* ── One anomaly result record ───────────────────────────────────────────── */
typedef struct {
    char  timestamp[64];    /* ISO-8601 or device log timestamp      */
    char  cmmac[32];        /* device CM MAC, colon-separated        */
    float cpu_mse;          /* CPU autoencoder reconstruction error  */
    float mem_mse;          /* Memory autoencoder reconstruction error */
    int   cpu_flag;         /* 1 = CPU anomaly detected              */
    int   mem_flag;         /* 1 = Memory anomaly detected           */
    float cpu_sev_ratio;    /* cpu_mse / cpu_threshold (>1.0 = anomaly) */
    float mem_sev_ratio;    /* mem_mse / mem_threshold               */
    char  anomaly_type[16]; /* Normal | CPU | Memory | Both          */
} AnomalyRecord;

/**
 * Map anomaly flag + severity ratio to a human-readable label.
 * Returns a static string: "normal" | "low" | "medium" | "high".
 *
 * Severity thresholds (ratio = MSE / model_threshold):
 *   flag = 0              → "normal"
 *   flag = 1, ratio < 2.0 → "low"
 *   flag = 1, ratio < 5.0 → "medium"
 *   flag = 1, ratio >= 5.0 → "high"
 */
const char *anomaly_severity_label(int flag, float ratio);

/**
 * Parse a severity label string to its integer rank.
 * Returns ANOMALY_SEV_RANK_* or -1 if unknown.
 */
int anomaly_severity_rank(const char *label);

/**
 * Read the most recent (last) record from the results CSV.
 * Returns 0 on success, -1 on error or no data available.
 */
int anomaly_csv_read_latest(const char *path, AnomalyRecord *out);

/**
 * Read up to `limit` records from the results CSV in newest-first order,
 * applying optional filters.
 *
 * @param path         Path to anomaly_results.csv
 * @param limit        Maximum records to return (1–200)
 * @param type_filter  Anomaly type: "cpu"|"memory"|"both"|"normal"|NULL
 * @param since        ISO-8601 lower bound timestamp (inclusive), or NULL
 * @param until        ISO-8601 upper bound timestamp (inclusive), or NULL
 * @param min_sev_rank Minimum ANOMALY_SEV_RANK_* (0 = include all)
 * @param out          Output: pointer to malloc'd array of AnomalyRecord
 * @param count        Output: number of records populated in *out
 *
 * Returns 0 on success, -1 on error.
 * Caller must call anomaly_csv_free_records(*out) when done.
 */
int anomaly_csv_read_history(const char *path, int limit,
                             const char *type_filter,
                             const char *since, const char *until,
                             int min_sev_rank,
                             AnomalyRecord **out, int *count);

/**
 * Free a records array returned by anomaly_csv_read_history().
 */
void anomaly_csv_free_records(AnomalyRecord *records);

/**
 * Count total inference rows and anomaly rows (non-Normal) in the CSV.
 * Used by the /anomaly/status endpoint to populate counters.
 * Returns 0 on success, -1 on error.
 */
int anomaly_csv_count_records(const char *path,
                              int *total_inferences,
                              int *anomaly_count);

#endif /* ANOMALY_SERVICE_CSV_H */
