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
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

static FILE *g_logFile = NULL;

static const char *level_to_string(LogLevel level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

static bool ensure_log_directory(void)
{
    struct stat st = {0};
    if (stat("/rdklogs/logs", &st) == -1) {
        if (mkdir("/rdklogs", 0755) == -1 && errno != EEXIST) {
            return false;
        }
        if (mkdir("/rdklogs/logs", 0755) == -1 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

void AnomalyService_Log(LogLevel level, const char *fmt, ...)
{
    if (!g_logFile) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(g_logFile, "[%s] [%s] ", timestamp, level_to_string(level));

    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);

    fprintf(g_logFile, "\n");
    fflush(g_logFile);
}

bool AnomalyService_Log_Init(void)
{
    if (!ensure_log_directory()) {
        fprintf(stderr, "AnomalyService: Failed to create log directory\n");
        return false;
    }

    g_logFile = fopen(ANOMALY_LOG_FILE, "a");
    if (!g_logFile) {
        fprintf(stderr, "AnomalyService: Failed to open %s: %s\n",
                ANOMALY_LOG_FILE, strerror(errno));
        return false;
    }

    AnomalySvcInfo("AnomalyService logging initialized");
    return true;
}

bool AnomalyService_Log_Deinit(void)
{
    if (g_logFile) {
        AnomalySvcInfo("AnomalyService logging shutdown");
        fclose(g_logFile);
        g_logFile = NULL;
    }
    return true;
}
