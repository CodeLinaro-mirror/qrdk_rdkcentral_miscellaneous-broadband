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

#ifndef ANOMALY_SERVICE_LOG_H
#define ANOMALY_SERVICE_LOG_H

#include <stdbool.h>

#define ANOMALY_LOG_FILE  "/rdklogs/logs/AnomalyService.txt"

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

#define AnomalySvcError(fmt, ...)  AnomalyService_Log(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define AnomalySvcWarn(fmt, ...)   AnomalyService_Log(LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define AnomalySvcInfo(fmt, ...)   AnomalyService_Log(LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define AnomalySvcDebug(fmt, ...)  AnomalyService_Log(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

bool AnomalyService_Log_Init(void);
bool AnomalyService_Log_Deinit(void);
void AnomalyService_Log(LogLevel level, const char *fmt, ...);

#endif /* ANOMALY_SERVICE_LOG_H */
