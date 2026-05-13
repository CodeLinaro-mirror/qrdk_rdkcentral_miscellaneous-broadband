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
 * anomaly_service_rbus.c
 * ─────────────────────────────────────────────────────────────────────────────
 * rbus integration for AnomalyService.
 *
 * Registers TR-181 parameters under Device.X_COMCAST_AnomalyEngine.* and
 * handles WebPA GET/SET requests forwarded by the CCSP framework.
 *
 * GET handlers read live state:
 *   - Engine status / PID → inspected from g_engine_pid (shared with main)
 *   - Inference results   → parsed from anomaly_results.csv on demand
 *
 * SET handlers:
 *   TriggerInference: spawns anomaly_app batch run (value 1=batch, 2=single)
 *   ResetState:       kills and restarts the anomaly_app daemon
 */

#include "anomaly_service_rbus.h"
#include "anomaly_service_csv.h"
#include "anomaly_service_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ── Shared state (defined in anomaly_service_main.c) ───────────────────── */
extern pid_t        g_engine_pid;
extern pid_t        g_trigger_pid;
extern char         g_anomaly_app_path[];
extern char         g_config_path[];
extern char         g_watch_path[];
extern char         g_result_path[];
extern pthread_mutex_t g_state_mutex;

/* ── Module-private ─────────────────────────────────────────────────────── */
static rbusHandle_t g_rbus_handle = NULL;

/* Forward declaration for trigger helper (also used by HTTP handler) */
int AnomalyService_TriggerBatch(const char *mode, const char *job_id);
int AnomalyService_ResetEngine(void);

#define ANOMALY_NUM_RBUS_PARAMS \
    (sizeof(s_rbus_elements) / sizeof(s_rbus_elements[0]))

static rbusDataElement_t s_rbus_elements[] = {
    /* TriggerInference: write 1 (batch) or 2 (single) to trigger */
    { ANOMALY_ENGINE_TRIGGER_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetUIntHandler, AnomalyEngine_SetUIntHandler,
        NULL, NULL, NULL, NULL } },

    /* InferenceMode: last used mode string */
    { ANOMALY_ENGINE_MODE_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetStringHandler, NULL, NULL, NULL, NULL, NULL } },

    /* EngineRunning: boolean */
    { ANOMALY_ENGINE_RUNNING_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetBoolHandler, NULL, NULL, NULL, NULL, NULL } },

    /* EnginePid: uint */
    { ANOMALY_ENGINE_PID_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetUIntHandler, NULL, NULL, NULL, NULL, NULL } },

    /* LastAnomalyType: string */
    { ANOMALY_ENGINE_LAST_TYPE_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetStringHandler, NULL, NULL, NULL, NULL, NULL } },

    /* LastInferenceAt: string (ISO-8601) */
    { ANOMALY_ENGINE_LAST_AT_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetStringHandler, NULL, NULL, NULL, NULL, NULL } },

    /* LastResult: string (JSON of most recent result row) */
    { ANOMALY_ENGINE_LAST_RESULT_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetStringHandler, NULL, NULL, NULL, NULL, NULL } },

    /* TotalInferences: uint (count of rows in CSV) */
    { ANOMALY_ENGINE_TOTAL_INF_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetUIntHandler, NULL, NULL, NULL, NULL, NULL } },

    /* AnomalyCount: uint (non-Normal rows) */
    { ANOMALY_ENGINE_ANOM_CNT_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetUIntHandler, NULL, NULL, NULL, NULL, NULL } },

    /* WarmupComplete: bool */
    { ANOMALY_ENGINE_WARMUP_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetBoolHandler, NULL, NULL, NULL, NULL, NULL } },

    /* ResetState: write 1 to reset rolling delta state */
    { ANOMALY_ENGINE_RESET_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetUIntHandler, AnomalyEngine_SetUIntHandler,
        NULL, NULL, NULL, NULL } },

    /* HistoryQuery: write JSON query params to request a filtered history page
     * e.g. {"limit":20,"type":"cpu","since":"2026-05-01T00:00:00Z","min_severity":"low"}
     * Triggers CSV scan; result is available via HistoryResult immediately after. */
    { ANOMALY_ENGINE_HISTORY_QUERY_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetStringHandler, AnomalyEngine_SetStringHandler,
        NULL, NULL, NULL, NULL } },

    /* HistoryResult: read the JSON array produced by the last HistoryQuery SET */
    { ANOMALY_ENGINE_HISTORY_RESULT_DML,
      RBUS_ELEMENT_TYPE_PROPERTY,
      { AnomalyEngine_GetStringHandler, NULL, NULL, NULL, NULL, NULL } },
};

/* Module-private: last history query result JSON (heap-allocated) */
static char *s_history_result = NULL;
static pthread_mutex_t s_history_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Rbus init / deinit ─────────────────────────────────────────────────── */

rbusError_t AnomalyService_Rbus_Init(void)
{
    int rc;
    AnomalySvcDebug(("In %s\n", __FUNCTION__));

    if (RBUS_ENABLED != rbus_checkStatus()) {
        AnomalySvcError(("rbus is not enabled; cannot register parameters\n"));
        return RBUS_ERROR_BUS_ERROR;
    }

    rc = rbus_open(&g_rbus_handle, ANOMALY_RBUS_COMPONENT_NAME);
    if (rc != RBUS_ERROR_SUCCESS) {
        AnomalySvcError(("rbus_open failed: %d\n", rc));
        return rc;
    }

    rc = rbus_regDataElements(g_rbus_handle,
                              (int)ANOMALY_NUM_RBUS_PARAMS,
                              s_rbus_elements);
    if (rc != RBUS_ERROR_SUCCESS) {
        AnomalySvcError(("rbus_regDataElements failed: %d\n", rc));
        rbus_close(g_rbus_handle);
        g_rbus_handle = NULL;
        return rc;
    }

    AnomalySvcInfo(("rbus registered %zu AnomalyEngine parameters\n",
                    ANOMALY_NUM_RBUS_PARAMS));
    return RBUS_ERROR_SUCCESS;
}

void AnomalyService_Rbus_Deinit(void)
{
    if (g_rbus_handle) {
        rbus_unregDataElements(g_rbus_handle,
                               (int)ANOMALY_NUM_RBUS_PARAMS,
                               s_rbus_elements);
        rbus_close(g_rbus_handle);
        g_rbus_handle = NULL;
    }
    pthread_mutex_lock(&s_history_mutex);
    free(s_history_result);
    s_history_result = NULL;
    pthread_mutex_unlock(&s_history_mutex);
}

/* ── Internal helpers ───────────────────────────────────────────────────── */

/* Check if a PID is alive using kill(pid, 0) */
static int pid_is_alive(pid_t pid)
{
    if (pid <= 0) return 0;
    return (kill(pid, 0) == 0) ? 1 : 0;
}

/* Build a compact JSON string from an AnomalyRecord into buf (size bufsz) */
static void record_to_json(const AnomalyRecord *r, char *buf, size_t bufsz)
{
    const char *cpu_sev = anomaly_severity_label(r->cpu_flag, r->cpu_sev_ratio);
    const char *mem_sev = anomaly_severity_label(r->mem_flag, r->mem_sev_ratio);

    snprintf(buf, bufsz,
             "{\"timestamp\":\"%s\","
             "\"device_mac\":\"%s\","
             "\"anomaly_type\":\"%s\","
             "\"cpu_mse\":%.6f,"
             "\"mem_mse\":%.6f,"
             "\"cpu_flag\":%s,"
             "\"mem_flag\":%s,"
             "\"cpu_severity\":\"%s\","
             "\"mem_severity\":\"%s\"}",
             r->timestamp, r->cmmac, r->anomaly_type,
             (double)r->cpu_mse, (double)r->mem_mse,
             r->cpu_flag ? "true" : "false",
             r->mem_flag ? "true" : "false",
             cpu_sev, mem_sev);
}

/* ── GET handlers ───────────────────────────────────────────────────────── */

rbusError_t AnomalyEngine_GetUIntHandler(rbusHandle_t handle,
                                         rbusProperty_t property,
                                         rbusGetHandlerOptions_t *opts)
{
    (void)handle; (void)opts;

    const char *name = rbusProperty_GetName(property);
    rbusValue_t val;
    rbusValue_Init(&val);

    AnomalySvcDebug(("GET %s\n", name));

    pthread_mutex_lock(&g_state_mutex);

    if (strcmp(name, ANOMALY_ENGINE_TRIGGER_DML) == 0 ||
        strcmp(name, ANOMALY_ENGINE_RESET_DML) == 0) {
        /* Write-only action params — always return 0 when read */
        rbusValue_SetUInt32(val, 0);

    } else if (strcmp(name, ANOMALY_ENGINE_PID_DML) == 0) {
        rbusValue_SetUInt32(val, (uint32_t)(pid_is_alive(g_engine_pid)
                                            ? g_engine_pid : 0));

    } else if (strcmp(name, ANOMALY_ENGINE_TOTAL_INF_DML) == 0 ||
               strcmp(name, ANOMALY_ENGINE_ANOM_CNT_DML) == 0) {
        int total = 0, anom = 0;
        anomaly_csv_count_records(g_result_path, &total, &anom);
        rbusValue_SetUInt32(val, (uint32_t)(
            strcmp(name, ANOMALY_ENGINE_TOTAL_INF_DML) == 0 ? total : anom));
    } else {
        rbusValue_SetUInt32(val, 0);
    }

    pthread_mutex_unlock(&g_state_mutex);

    rbusProperty_SetValue(property, val);
    rbusValue_Release(val);
    return RBUS_ERROR_SUCCESS;
}

rbusError_t AnomalyEngine_GetBoolHandler(rbusHandle_t handle,
                                         rbusProperty_t property,
                                         rbusGetHandlerOptions_t *opts)
{
    (void)handle; (void)opts;

    const char *name = rbusProperty_GetName(property);
    rbusValue_t val;
    rbusValue_Init(&val);

    AnomalySvcDebug(("GET %s\n", name));

    pthread_mutex_lock(&g_state_mutex);

    if (strcmp(name, ANOMALY_ENGINE_RUNNING_DML) == 0) {
        rbusValue_SetBoolean(val, pid_is_alive(g_engine_pid) ? 1 : 0);

    } else if (strcmp(name, ANOMALY_ENGINE_WARMUP_DML) == 0) {
        int total = 0, anom = 0;
        anomaly_csv_count_records(g_result_path, &total, &anom);
        rbusValue_SetBoolean(val, (total >= ANOMALY_WARMUP_SAMPLES) ? 1 : 0);
    } else {
        rbusValue_SetBoolean(val, 0);
    }

    pthread_mutex_unlock(&g_state_mutex);

    rbusProperty_SetValue(property, val);
    rbusValue_Release(val);
    return RBUS_ERROR_SUCCESS;
}

rbusError_t AnomalyEngine_GetStringHandler(rbusHandle_t handle,
                                           rbusProperty_t property,
                                           rbusGetHandlerOptions_t *opts)
{
    (void)handle; (void)opts;

    const char *name = rbusProperty_GetName(property);
    rbusValue_t val;
    rbusValue_Init(&val);

    char strbuf[2048] = {0};

    AnomalySvcDebug(("GET %s\n", name));

    if (strcmp(name, ANOMALY_ENGINE_MODE_DML) == 0) {
        snprintf(strbuf, sizeof(strbuf), "batch");

    } else if (strcmp(name, ANOMALY_ENGINE_HISTORY_QUERY_DML) == 0) {
        /* Write-only logically; return empty string when read */
        strbuf[0] = '\0';

    } else if (strcmp(name, ANOMALY_ENGINE_HISTORY_RESULT_DML) == 0) {
        pthread_mutex_lock(&s_history_mutex);
        if (s_history_result)
            snprintf(strbuf, sizeof(strbuf), "%s", s_history_result);
        pthread_mutex_unlock(&s_history_mutex);

    } else if (strcmp(name, ANOMALY_ENGINE_LAST_TYPE_DML) == 0 ||
               strcmp(name, ANOMALY_ENGINE_LAST_AT_DML)   == 0 ||
               strcmp(name, ANOMALY_ENGINE_LAST_RESULT_DML) == 0) {

        AnomalyRecord r;
        if (anomaly_csv_read_latest(g_result_path, &r) == 0) {
            if (strcmp(name, ANOMALY_ENGINE_LAST_TYPE_DML) == 0) {
                snprintf(strbuf, sizeof(strbuf), "%s", r.anomaly_type);
            } else if (strcmp(name, ANOMALY_ENGINE_LAST_AT_DML) == 0) {
                snprintf(strbuf, sizeof(strbuf), "%s", r.timestamp);
            } else {
                record_to_json(&r, strbuf, sizeof(strbuf));
            }
        } else {
            strbuf[0] = '\0';
        }
    }

    rbusValue_SetString(val, strbuf);
    rbusProperty_SetValue(property, val);
    rbusValue_Release(val);
    return RBUS_ERROR_SUCCESS;
}

/* ── SET handlers ───────────────────────────────────────────────────────── */

rbusError_t AnomalyEngine_SetUIntHandler(rbusHandle_t handle,
                                         rbusProperty_t property,
                                         rbusSetHandlerOptions_t *opts)
{
    (void)handle; (void)opts;

    const char *name = rbusProperty_GetName(property);
    rbusValue_t val  = rbusProperty_GetValue(property);
    uint32_t    v    = rbusValue_GetUInt32(val);

    AnomalySvcInfo(("SET %s = %u\n", name, v));

    if (strcmp(name, ANOMALY_ENGINE_TRIGGER_DML) == 0) {
        if (v == 0) return RBUS_ERROR_SUCCESS;
        const char *mode = (v == 2) ? "single" : "batch";
        if (AnomalyService_TriggerBatch(mode, NULL) != 0) {
            AnomalySvcError(("TriggerInference SET failed\n"));
            return RBUS_ERROR_BUS_ERROR;
        }

    } else if (strcmp(name, ANOMALY_ENGINE_RESET_DML) == 0) {
        if (v == 1) {
            if (AnomalyService_ResetEngine() != 0) {
                AnomalySvcError(("ResetState SET failed\n"));
                return RBUS_ERROR_BUS_ERROR;
            }
        }
    }

    return RBUS_ERROR_SUCCESS;
}

/* ── HistoryQuery SET handler ───────────────────────────────────────────────
 * Accepts a JSON query object:
 *   {"limit":20,"type":"cpu","since":"ISO","until":"ISO","min_severity":"low"}
 * Runs the CSV scan synchronously and stores the result JSON in s_history_result
 * for subsequent retrieval via the HistoryResult GET handler.
 * ────────────────────────────────────────────────────────────────────────── */
rbusError_t AnomalyEngine_SetStringHandler(rbusHandle_t handle,
                                           rbusProperty_t property,
                                           rbusSetHandlerOptions_t *opts)
{
    (void)handle; (void)opts;

    const char *name = rbusProperty_GetName(property);
    rbusValue_t val  = rbusProperty_GetValue(property);
    const char *query_json = rbusValue_GetString(val, NULL);

    AnomalySvcInfo(("SET %s = %s\n", name, query_json ? query_json : "(null)"));

    if (strcmp(name, ANOMALY_ENGINE_HISTORY_QUERY_DML) != 0)
        return RBUS_ERROR_SUCCESS;

    if (!query_json || query_json[0] == '\0')
        return RBUS_ERROR_INVALID_INPUT;

    /* ── Parse JSON fields (simple substring search — no external JSON lib) ── */
    int   limit            = 20;
    char  type_filter[32]  = "";
    char  since[64]        = "";
    char  until[64]        = "";
    char  min_sev[16]      = "";

    /* Helper: extract quoted string value for a JSON key */
#define JSON_STR(key, buf, bufsz) do { \
    const char *_p = strstr(query_json, "\"" key "\""); \
    if (_p) { _p = strchr(_p + strlen(key) + 2, '"'); \
        if (_p) { _p++; size_t _i = 0; \
            while (*_p && *_p != '"' && _i < (bufsz) - 1) (buf)[_i++] = *_p++; \
            (buf)[_i] = '\0'; } } } while (0)

    JSON_STR("type",         type_filter, sizeof(type_filter));
    JSON_STR("since",        since,       sizeof(since));
    JSON_STR("until",        until,       sizeof(until));
    JSON_STR("min_severity", min_sev,     sizeof(min_sev));

    /* Extract integer "limit" */
    const char *lp = strstr(query_json, "\"limit\"");
    if (lp) {
        lp = strchr(lp + 7, ':');
        if (lp) { int v = atoi(lp + 1); if (v >= 1 && v <= 200) limit = v; }
    }

    int min_rank = anomaly_severity_rank(min_sev[0] ? min_sev : NULL);
    if (min_rank < 0) min_rank = ANOMALY_SEV_RANK_NORMAL;

    AnomalyRecord *records = NULL;
    int count = 0;
    anomaly_csv_read_history(g_result_path, limit,
                             type_filter[0] ? type_filter : NULL,
                             since[0] ? since : NULL,
                             until[0] ? until : NULL,
                             min_rank, &records, &count);

    /* Build JSON result: {"device_mac":"...","total_returned":N,"events":[...]} */
    char mac[32] = "";
    AnomalyRecord latest;
    if (anomaly_csv_read_latest(g_result_path, &latest) == 0)
        snprintf(mac, sizeof(mac), "%s", latest.cmmac);

    size_t bufsz = (size_t)(count * 350 + 256);
    char  *result_buf = (char *)malloc(bufsz);
    if (!result_buf) {
        anomaly_csv_free_records(records);
        return RBUS_ERROR_OUT_OF_RESOURCES;
    }

    int pos = 0;
    pos += snprintf(result_buf + pos, bufsz - (size_t)pos,
                    "{\"device_mac\":\"%s\",\"total_returned\":%d,\"events\":[",
                    mac, count);

    for (int i = 0; i < count && (size_t)pos < bufsz - 300; i++) {
        const AnomalyRecord *r = &records[i];
        const char *cpu_sev = anomaly_severity_label(r->cpu_flag, r->cpu_sev_ratio);
        const char *mem_sev = anomaly_severity_label(r->mem_flag, r->mem_sev_ratio);
        pos += snprintf(result_buf + pos, bufsz - (size_t)pos,
                        "%s{\"timestamp\":\"%s\",\"device_mac\":\"%s\","
                        "\"anomaly_type\":\"%s\","
                        "\"cpu_mse\":%.6f,\"mem_mse\":%.6f,"
                        "\"cpu_flag\":%s,\"mem_flag\":%s,"
                        "\"cpu_severity\":\"%s\",\"mem_severity\":\"%s\"}",
                        (i > 0) ? "," : "",
                        r->timestamp, r->cmmac, r->anomaly_type,
                        (double)r->cpu_mse, (double)r->mem_mse,
                        r->cpu_flag ? "true" : "false",
                        r->mem_flag ? "true" : "false",
                        cpu_sev, mem_sev);
    }
    snprintf(result_buf + pos, bufsz - (size_t)pos, "]}");

    anomaly_csv_free_records(records);

    pthread_mutex_lock(&s_history_mutex);
    free(s_history_result);
    s_history_result = result_buf;
    pthread_mutex_unlock(&s_history_mutex);

    AnomalySvcInfo(("HistoryQuery: built result count=%d\n", count));
    return RBUS_ERROR_SUCCESS;
}
