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
 * anomaly_service_rbus.h
 * ─────────────────────────────────────────────────────────────────────────────
 * TR-181 data model parameter definitions and rbus handler declarations for
 * the AnomalyService component.
 *
 * TR-181 parameters exposed under Device.X_COMCAST_AnomalyEngine.*:
 *
 *   TriggerInference   (uint, writable) — write 1=batch, 2=single to trigger
 *   InferenceMode      (string)         — last used mode: "batch" | "single"
 *   EngineRunning      (bool)           — true if anomaly_app daemon is alive
 *   EnginePid          (uint)           — PID of the running daemon, 0 if not
 *   LastAnomalyType    (string)         — last result: Normal|CPU|Memory|Both
 *   LastInferenceAt    (string)         — ISO-8601 timestamp of last result
 *   LastResult         (string)         — JSON of the most recent result row
 *   TotalInferences    (uint)           — total rows in anomaly_results.csv
 *   AnomalyCount       (uint)           — non-Normal rows in results CSV
 *   WarmupComplete     (bool)           — true after >= 3 inferences
 *   ResetState         (uint, writable) — write 1 to reset rolling delta state
 */

#ifndef ANOMALY_SERVICE_RBUS_H
#define ANOMALY_SERVICE_RBUS_H

#include <rbus/rbus.h>

/* ── TR-181 DML parameter paths ─────────────────────────────────────────── */
#define ANOMALY_ENGINE_TRIGGER_DML      "Device.X_COMCAST_AnomalyEngine.TriggerInference"
#define ANOMALY_ENGINE_MODE_DML         "Device.X_COMCAST_AnomalyEngine.InferenceMode"
#define ANOMALY_ENGINE_RUNNING_DML      "Device.X_COMCAST_AnomalyEngine.EngineRunning"
#define ANOMALY_ENGINE_PID_DML          "Device.X_COMCAST_AnomalyEngine.EnginePid"
#define ANOMALY_ENGINE_LAST_TYPE_DML    "Device.X_COMCAST_AnomalyEngine.LastAnomalyType"
#define ANOMALY_ENGINE_LAST_AT_DML      "Device.X_COMCAST_AnomalyEngine.LastInferenceAt"
#define ANOMALY_ENGINE_LAST_RESULT_DML  "Device.X_COMCAST_AnomalyEngine.LastResult"
#define ANOMALY_ENGINE_TOTAL_INF_DML    "Device.X_COMCAST_AnomalyEngine.TotalInferences"
#define ANOMALY_ENGINE_ANOM_CNT_DML     "Device.X_COMCAST_AnomalyEngine.AnomalyCount"
#define ANOMALY_ENGINE_WARMUP_DML       "Device.X_COMCAST_AnomalyEngine.WarmupComplete"
#define ANOMALY_ENGINE_RESET_DML        "Device.X_COMCAST_AnomalyEngine.ResetState"
#define ANOMALY_ENGINE_HISTORY_QUERY_DML "Device.X_COMCAST_AnomalyEngine.HistoryQuery"
#define ANOMALY_ENGINE_HISTORY_RESULT_DML "Device.X_COMCAST_AnomalyEngine.HistoryResult"

#define ANOMALY_RBUS_COMPONENT_NAME     "AnomalyServiceRbus"

/* ── Warmup threshold — mirrors anomaly_app default warmup_samples ───────── */
#define ANOMALY_WARMUP_SAMPLES  3

/**
 * Initialise rbus and register all TR-181 data elements.
 * Must be called after the daemon has forked and logging is ready.
 * Returns RBUS_ERROR_SUCCESS on success.
 */
rbusError_t AnomalyService_Rbus_Init(void);

/**
 * Unregister data elements and close the rbus handle.
 */
void AnomalyService_Rbus_Deinit(void);

/* ── GET / SET handler declarations (implemented in anomaly_service_rbus.c) ─ */
rbusError_t AnomalyEngine_GetUIntHandler(rbusHandle_t handle,
                                         rbusProperty_t property,
                                         rbusGetHandlerOptions_t *opts);

rbusError_t AnomalyEngine_SetUIntHandler(rbusHandle_t handle,
                                         rbusProperty_t property,
                                         rbusSetHandlerOptions_t *opts);

rbusError_t AnomalyEngine_GetStringHandler(rbusHandle_t handle,
                                           rbusProperty_t property,
                                           rbusGetHandlerOptions_t *opts);

rbusError_t AnomalyEngine_SetStringHandler(rbusHandle_t handle,
                                           rbusProperty_t property,
                                           rbusSetHandlerOptions_t *opts);

rbusError_t AnomalyEngine_GetBoolHandler(rbusHandle_t handle,
                                         rbusProperty_t property,
                                         rbusGetHandlerOptions_t *opts);

#endif /* ANOMALY_SERVICE_RBUS_H */
