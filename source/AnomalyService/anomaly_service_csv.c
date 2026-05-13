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
 */

#include "anomaly_service_csv.h"
#include "anomaly_service_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */

#define ANOMALY_MAX_LINE_LEN  512

/* ── Severity mapping ───────────────────────────────────────────────────────
 * dense_cpu_sev / dense_mem_sev in the CSV are float ratios: MSE / threshold.
 * Values > 1.0 indicate the anomaly threshold was exceeded.
 * ────────────────────────────────────────────────────────────────────────── */
const char *anomaly_severity_label(int flag, float ratio)
{
    if (!flag)          return "normal";
    if (ratio < 2.0f)   return "low";
    if (ratio < 5.0f)   return "medium";
    return "high";
}

int anomaly_severity_rank(const char *label)
{
    if (!label)                          return ANOMALY_SEV_RANK_NORMAL;
    if (strcasecmp(label, "normal") == 0) return ANOMALY_SEV_RANK_NORMAL;
    if (strcasecmp(label, "low")    == 0) return ANOMALY_SEV_RANK_LOW;
    if (strcasecmp(label, "medium") == 0) return ANOMALY_SEV_RANK_MEDIUM;
    if (strcasecmp(label, "high")   == 0) return ANOMALY_SEV_RANK_HIGH;
    return -1;
}

/* ── Internal: strip trailing CR/LF from a string in place ─────────────── */
static void strip_crlf(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

/* ── Internal: parse one CSV data line into AnomalyRecord ──────────────────
 * Expected columns (from anomaly_app_main.cc WriteResultRow):
 *   0:timestamp  1:CMMAC  2:cpu_mse  3:mem_mse  4:cpu_flag  5:mem_flag
 *   6:cpu_sev    7:mem_sev  8:anomaly_type
 * Returns 0 on success, -1 if line is a header row or malformed.
 * ────────────────────────────────────────────────────────────────────────── */
static int parse_csv_line(const char *line, AnomalyRecord *r)
{
    char buf[ANOMALY_MAX_LINE_LEN];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    strip_crlf(buf);

    if (buf[0] == '\0')
        return -1;

    /* Reject header row: first field is the literal word "timestamp" */
    if (strncmp(buf, "timestamp", 9) == 0)
        return -1;

    /* Tokenise on commas — exactly 9 fields expected */
    char *fields[9];
    int   nf = 0;
    char *p  = buf;

    while (nf < 9) {
        fields[nf++] = p;
        p = strchr(p, ',');
        if (!p) break;
        *p++ = '\0';
    }

    if (nf < 9)
        return -1;

    snprintf(r->timestamp,    sizeof(r->timestamp),    "%s", fields[0]);
    snprintf(r->cmmac,        sizeof(r->cmmac),        "%s", fields[1]);
    r->cpu_mse       = (float)atof(fields[2]);
    r->mem_mse       = (float)atof(fields[3]);
    r->cpu_flag      = atoi(fields[4]);
    r->mem_flag      = atoi(fields[5]);
    r->cpu_sev_ratio = (float)atof(fields[6]);
    r->mem_sev_ratio = (float)atof(fields[7]);
    snprintf(r->anomaly_type, sizeof(r->anomaly_type), "%s", fields[8]);

    return 0;
}

/* ── Internal: read last `n` non-header data lines in newest-first order ──
 *
 * Seeks to near end of file (heuristic offset), reads into a ring buffer,
 * and returns the last n lines with the most recent first.
 *
 * Ring buffer size = n+1 to distinguish full/empty states.
 * lines_out is allocated by this function; caller must free it.
 * ────────────────────────────────────────────────────────────────────────── */
typedef char AnomalyLine[ANOMALY_MAX_LINE_LEN];

static int read_last_lines(const char *path, int n,
                           AnomalyLine **lines_out, int *count)
{
    *lines_out = NULL;
    *count     = 0;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        AnomalySvcWarn(("anomaly_csv: cannot open %s\n", path));
        return -1;
    }

    /* Each result line is at most ~200 bytes; read 2× n lines from the end */
    fseek(fp, 0, SEEK_END);
    long fsize    = ftell(fp);
    long scan_len = (long)(n + 2) * 250L;
    long start    = (fsize > scan_len) ? fsize - scan_len : 0L;
    fseek(fp, start, SEEK_SET);

    /* If we landed mid-line, discard the partial first line */
    if (start > 0) {
        char tmp[ANOMALY_MAX_LINE_LEN];
        if (!fgets(tmp, sizeof(tmp), fp)) {
            fclose(fp);
            return 0;  /* empty scan region — not an error */
        }
    }

    int ring_sz = n + 1;
    AnomalyLine *ring = (AnomalyLine *)calloc(ring_sz, sizeof(AnomalyLine));
    if (!ring) { fclose(fp); return -1; }

    int head  = 0;
    int total = 0;
    char linebuf[ANOMALY_MAX_LINE_LEN];

    while (fgets(linebuf, sizeof(linebuf), fp)) {
        strip_crlf(linebuf);
        if (linebuf[0] == '\0') continue;
        if (strncmp(linebuf, "timestamp", 9) == 0) continue;  /* header */

        memcpy(ring[head % ring_sz], linebuf, ANOMALY_MAX_LINE_LEN);
        head++;
        total++;
    }
    fclose(fp);

    /* Number of useful lines we stored */
    int c = (total < n) ? total : n;

    AnomalyLine *result = (AnomalyLine *)calloc(c, sizeof(AnomalyLine));
    if (!result) { free(ring); return -1; }

    /* Produce newest-first order: index (head-1) is newest */
    for (int i = 0; i < c; i++) {
        int idx = ((head - 1 - i) % ring_sz + ring_sz) % ring_sz;
        memcpy(result[i], ring[idx], ANOMALY_MAX_LINE_LEN);
    }

    free(ring);
    *lines_out = result;
    *count     = c;
    return 0;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int anomaly_csv_read_latest(const char *path, AnomalyRecord *out)
{
    if (!path || !out) return -1;

    AnomalyLine *lines = NULL;
    int count = 0;

    if (read_last_lines(path, 1, &lines, &count) != 0 || count == 0) {
        free(lines);
        return -1;
    }

    int rc = parse_csv_line(lines[0], out);
    free(lines);
    return rc;
}

/* ── Filter helpers ─────────────────────────────────────────────────────── */

static int type_matches(const AnomalyRecord *r, const char *filter)
{
    if (!filter || filter[0] == '\0') return 1;
    return (strcasecmp(r->anomaly_type, filter) == 0);
}

/* Timestamp comparison: lexicographic on the first 19 chars (YYYY-MM-DD...) */
static int ts_ge(const char *ts, const char *since)
{
    if (!since || since[0] == '\0') return 1;
    return strncmp(ts, since, 19) >= 0;
}

static int ts_le(const char *ts, const char *until)
{
    if (!until || until[0] == '\0') return 1;
    return strncmp(ts, until, 19) <= 0;
}

static int severity_ge(const AnomalyRecord *r, int min_rank)
{
    if (min_rank <= ANOMALY_SEV_RANK_NORMAL) return 1;

    int cpu_rank = anomaly_severity_rank(
                       anomaly_severity_label(r->cpu_flag, r->cpu_sev_ratio));
    int mem_rank = anomaly_severity_rank(
                       anomaly_severity_label(r->mem_flag, r->mem_sev_ratio));
    int max_rank = (cpu_rank > mem_rank) ? cpu_rank : mem_rank;
    return (max_rank >= min_rank);
}

int anomaly_csv_read_history(const char *path, int limit,
                             const char *type_filter,
                             const char *since, const char *until,
                             int min_sev_rank,
                             AnomalyRecord **out, int *count)
{
    if (!path || !out || !count) return -1;
    *out   = NULL;
    *count = 0;

    /* Fetch more lines than limit to satisfy filters after skipping */
    int fetch = limit * 4 + 64;
    if (fetch > 2000) fetch = 2000;

    AnomalyLine *lines = NULL;
    int total_fetched  = 0;

    if (read_last_lines(path, fetch, &lines, &total_fetched) != 0) {
        return -1;
    }

    AnomalyRecord *records = (AnomalyRecord *)calloc(limit, sizeof(AnomalyRecord));
    if (!records) { free(lines); return -1; }

    int c = 0;
    for (int i = 0; i < total_fetched && c < limit; i++) {
        AnomalyRecord r;
        if (parse_csv_line(lines[i], &r) != 0)          continue;
        if (!type_matches(&r, type_filter))              continue;
        if (!ts_ge(r.timestamp, since))                  continue;
        if (!ts_le(r.timestamp, until))                  continue;
        if (!severity_ge(&r, min_sev_rank))              continue;
        records[c++] = r;
    }

    free(lines);
    *out   = records;
    *count = c;
    return 0;
}

void anomaly_csv_free_records(AnomalyRecord *records)
{
    free(records);
}

int anomaly_csv_count_records(const char *path,
                              int *total_inferences, int *anomaly_count)
{
    if (!path || !total_inferences || !anomaly_count) return -1;
    *total_inferences = 0;
    *anomaly_count    = 0;

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[ANOMALY_MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        strip_crlf(line);
        if (line[0] == '\0') continue;
        if (strncmp(line, "timestamp", 9) == 0) continue;  /* header */

        (*total_inferences)++;

        /* anomaly_type is the last comma-delimited field */
        char *last = strrchr(line, ',');
        if (last) {
            last++;
            /* Trim trailing whitespace */
            while (*last == ' ') last++;
            if (strncasecmp(last, "Normal", 6) != 0)
                (*anomaly_count)++;
        }
    }
    fclose(fp);
    return 0;
}
