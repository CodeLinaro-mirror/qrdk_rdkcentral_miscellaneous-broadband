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

#include "anomaly_service_log.h"
#include <unistd.h>
#include <stdio.h>

bool AnomalyService_Log_Init(void)
{
    const char *ini = (access(DEBUG_INI_OVERRIDE_PATH, F_OK) == 0)
                          ? DEBUG_INI_OVERRIDE_PATH
                          : DEBUG_INI_NAME;

    if (rdk_logger_init(ini) != RDK_SUCCESS) {
        fprintf(stderr, "AnomalyService: rdk_logger_init(%s) failed\n", ini);
        return false;
    }
    return true;
}

bool AnomalyService_Log_Deinit(void)
{
    return (rdk_logger_deinit() == RDK_SUCCESS);
}
