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
#include "rdk_debug.h"

#define DEBUG_INI_NAME          "/etc/debug.ini"
#define DEBUG_INI_OVERRIDE_PATH "/nvram/debug.ini"
#define ARGS_EXTRACT(msg ...)    msg

#define ANOMALY_SVC_LOG(level, msg) \
    RDK_LOG(level, "LOG.RDK.ANOMALYSVC", ARGS_EXTRACT msg)

#define AnomalySvcError(msg)    ANOMALY_SVC_LOG(RDK_LOG_ERROR, msg)
#define AnomalySvcInfo(msg)     ANOMALY_SVC_LOG(RDK_LOG_INFO,  msg)
#define AnomalySvcWarn(msg)     ANOMALY_SVC_LOG(RDK_LOG_WARN,  msg)
#define AnomalySvcDebug(msg)    ANOMALY_SVC_LOG(RDK_LOG_DEBUG, msg)

bool AnomalyService_Log_Init(void);
bool AnomalyService_Log_Deinit(void);

#endif /* ANOMALY_SERVICE_LOG_H */
