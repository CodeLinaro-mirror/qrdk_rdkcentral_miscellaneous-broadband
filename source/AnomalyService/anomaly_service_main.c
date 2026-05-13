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
 * anomaly_service_main.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Main daemon for the AnomalyService component.
 *
 * Responsibilities:
 *   1. Daemonise the process.
 *   2. Initialise RDK logging and rbus (TR-181 parameters).
 *   3. Spawn and monitor anomaly_app in file-watcher daemon mode.
 *   4. Implement AnomalyService_TriggerBatch() and AnomalyService_ResetEngine()
 *      — invoked by rbus SET handlers when WebPA writes TR-181 parameters.
 *   5. Handle SIGTERM / SIGINT for clean shutdown.
 *
 * anomaly_app integration:
 *   - Daemon mode (file-watcher):
 *       anomaly_app -c  <cpu_config>
 *                   --memory-config <mem_config>
 *                   --watch-path    <telemetry_csv>
 *                   --result-path   <result_csv>
 *                   -d  <delegate.so>
 *                   --delegate-options "bstm:1;bstm-client-mode:0;dynamic-tensors:1"
 *     Started on AnomalyService boot; restarted on failure or reset.
 *
 *   - Batch trigger (one-shot):
 *       anomaly_app -c  <cpu_config>
 *                   --memory-config <mem_config>
 *                   --input         <telemetry_csv>
 *                   --output        <result_csv>
 *                   -d  <delegate.so>
 *                   --delegate-options "bstm:1;bstm-client-mode:0;dynamic-tensors:1"
 *                   --skip-existing
 *     One-shot run launched per trigger request; results appended to the
 *     same result CSV that rbus GET handlers read.
 */

#include "anomaly_service_log.h"
#include "anomaly_service_rbus.h"
#include "anomaly_service_csv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/* ── Build-time configurable defaults ───────────────────────────────────── */
#ifndef ANOMALY_APP_BINARY
#define ANOMALY_APP_BINARY          "/usr/bin/anomaly_app"
#endif

#ifndef ANOMALY_CPU_CONFIG
#define ANOMALY_CPU_CONFIG          "/usr/share/tflite_model/cpu_anomaly_config.json"
#endif

#ifndef ANOMALY_MEM_CONFIG
#define ANOMALY_MEM_CONFIG          "/usr/share/tflite_model/memory_anomaly_config.json"
#endif

#ifndef ANOMALY_TELEMETRY_CSV
#define ANOMALY_TELEMETRY_CSV       "/rdklogs/logs/system_stats_data.csv"
#endif

#ifndef ANOMALY_RESULT_CSV
#define ANOMALY_RESULT_CSV          "/rdklogs/logs/anomaly_results.csv"
#endif

#ifndef ANOMALY_DELEGATE_PATH
#define ANOMALY_DELEGATE_PATH       "/usr/lib/libbstorm_external_delegate.so"
#endif

#ifndef ANOMALY_DELEGATE_OPTIONS
#define ANOMALY_DELEGATE_OPTIONS    "bstm:1;bstm-client-mode:0;dynamic-tensors:1"
#endif

/* Restart anomaly_app daemon if it exits unexpectedly */
#define ANOMALY_RESTART_DELAY_S  5

/* ── Global shared state (extern in rbus / http units) ──────────────────── */
volatile sig_atomic_t g_running    = 1;
pid_t                 g_engine_pid = -1;  /* anomaly_app daemon PID   */
pid_t                 g_trigger_pid = -1; /* one-shot batch job PID   */
char  g_anomaly_app_path[256];
char  g_config_path[256];           /* CPU model config   (-c)              */
char  g_mem_config_path[256];       /* memory model config (--memory-config) */
char  g_watch_path[256];            /* telemetry CSV to watch               */
char  g_result_path[256];           /* results CSV path                     */
char  g_delegate_path[256];         /* TFLite external delegate .so  (-d)   */
char  g_delegate_options[512];      /* delegate option string               */
pthread_mutex_t g_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Signal handler ─────────────────────────────────────────────────────── */
static void sig_handler(int sig)
{
    if (sig == SIGTERM || sig == SIGINT) {
        g_running = 0;
    } else if (sig == SIGCHLD) {
        /* Reap any zombie children without blocking */
        while (waitpid(-1, NULL, WNOHANG) > 0)
            ;
    }
    /* Re-install to remain POSIX-compliant on older platforms */
    signal(sig, sig_handler);
}

/* ── Daemonise ──────────────────────────────────────────────────────────── */
static void daemonize(void)
{
    switch (fork()) {
    case 0:  break;          /* child continues */
    case -1: exit(1);        /* fork failed     */
    default: _exit(0);       /* parent exits    */
    }

    if (setsid() < 0)
        exit(1);

#ifndef _DEBUG
    int fd = open("/dev/null", O_RDONLY);
    if (fd != 0) { dup2(fd, 0); close(fd); }
    fd = open("/dev/null", O_WRONLY);
    if (fd != 1) { dup2(fd, 1); close(fd); }
    fd = open("/dev/null", O_WRONLY);
    if (fd != 2) { dup2(fd, 2); close(fd); }
#endif
}

/* ── anomaly_app process management ────────────────────────────────────── */

/**
 * Spawn anomaly_app in continuous file-watcher daemon mode.
 * The child process watches g_watch_path and appends results to g_result_path.
 * Returns the child PID on success, -1 on failure.
 */
static pid_t spawn_engine_daemon(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        AnomalySvcError(("spawn_engine_daemon: fork failed: %s\n",
                         strerror(errno)));
        return -1;
    }

    if (pid == 0) {
        /* Child: exec anomaly_app in watcher mode */
        char *argv[20];
        int   i = 0;

        argv[i++] = g_anomaly_app_path;

        /* CPU model config */
        if (g_config_path[0] != '\0') {
            argv[i++] = "-c";
            argv[i++] = g_config_path;
        }

        /* Memory model config */
        if (g_mem_config_path[0] != '\0') {
            argv[i++] = "--memory-config";
            argv[i++] = g_mem_config_path;
        }

        argv[i++] = "--watch-path";
        argv[i++] = g_watch_path;
        argv[i++] = "--result-path";
        argv[i++] = g_result_path;

        /* External TFLite delegate */
        if (g_delegate_path[0] != '\0') {
            argv[i++] = "-d";
            argv[i++] = g_delegate_path;
            if (g_delegate_options[0] != '\0') {
                argv[i++] = "--delegate-options";
                argv[i++] = g_delegate_options;
            }
        }

        argv[i] = NULL;

        execv(g_anomaly_app_path, argv);
        /* execv only returns on error */
        _exit(127);
    }

    AnomalySvcInfo(("anomaly_app daemon started pid=%d\n", (int)pid));
    return pid;
}

/**
 * AnomalyService_TriggerBatch — spawn a one-shot anomaly_app batch run.
 *
 * Reads the current telemetry CSV and appends new result rows.
 * Uses --skip-existing so only rows written after this call are processed,
 * preventing re-inference of historical data.
 *
 * Called from both the rbus SET handler and the HTTP POST /anomaly/trigger.
 */
int AnomalyService_TriggerBatch(const char *mode, const char *job_id)
{
    (void)mode;   /* mode is informational; anomaly_app batch always runs full */
    (void)job_id;

    pthread_mutex_lock(&g_state_mutex);

    /* If a previous trigger job is still running, report busy */
    if (g_trigger_pid > 0 && kill(g_trigger_pid, 0) == 0) {
        pthread_mutex_unlock(&g_state_mutex);
        AnomalySvcWarn(("TriggerBatch: previous job pid=%d still running\n",
                        (int)g_trigger_pid));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        pthread_mutex_unlock(&g_state_mutex);
        AnomalySvcError(("TriggerBatch: fork failed: %s\n", strerror(errno)));
        return -1;
    }

    if (pid == 0) {
        /* Child: exec anomaly_app in batch mode */
        char *argv[20];
        int   i = 0;

        argv[i++] = g_anomaly_app_path;

        /* CPU model config */
        if (g_config_path[0] != '\0') {
            argv[i++] = "-c";
            argv[i++] = g_config_path;
        }

        /* Memory model config */
        if (g_mem_config_path[0] != '\0') {
            argv[i++] = "--memory-config";
            argv[i++] = g_mem_config_path;
        }

        argv[i++] = "--input";
        argv[i++] = g_watch_path;
        argv[i++] = "--output";
        argv[i++] = g_result_path;

        /* External TFLite delegate */
        if (g_delegate_path[0] != '\0') {
            argv[i++] = "-d";
            argv[i++] = g_delegate_path;
            if (g_delegate_options[0] != '\0') {
                argv[i++] = "--delegate-options";
                argv[i++] = g_delegate_options;
            }
        }

        /* Process only rows that arrive after trigger time */
        argv[i++] = "--skip-existing";
        argv[i]   = NULL;

        execv(g_anomaly_app_path, argv);
        _exit(127);
    }

    g_trigger_pid = pid;
    pthread_mutex_unlock(&g_state_mutex);

    AnomalySvcInfo(("TriggerBatch: spawned pid=%d mode=%s job_id=%s\n",
                    (int)pid, mode ? mode : "batch",
                    job_id ? job_id : "(none)"));
    return 0;
}

/**
 * AnomalyService_ResetEngine — restart anomaly_app daemon.
 *
 * Terminates the running daemon (which clears all in-memory DeviceState) and
 * respawns it. This resets the rolling delta accumulators used for delta-
 * feature computation, preventing stale deltas after a CPE reboot.
 *
 * Called from both the rbus SET handler and the HTTP POST /anomaly/reset.
 */
int AnomalyService_ResetEngine(void)
{
    pthread_mutex_lock(&g_state_mutex);

    if (g_engine_pid > 0) {
        AnomalySvcInfo(("ResetEngine: terminating daemon pid=%d\n",
                        (int)g_engine_pid));
        kill(g_engine_pid, SIGTERM);

        /* Wait up to 3 seconds for graceful exit */
        for (int i = 0; i < 30; i++) {
            usleep(100000);   /* 100ms */
            if (kill(g_engine_pid, 0) != 0)
                break;
        }

        /* Force-kill if still alive */
        if (kill(g_engine_pid, 0) == 0) {
            AnomalySvcWarn(("ResetEngine: daemon did not exit; sending SIGKILL\n"));
            kill(g_engine_pid, SIGKILL);
        }
        waitpid(g_engine_pid, NULL, WNOHANG);
        g_engine_pid = -1;
    }

    /* Respawn with fresh state */
    g_engine_pid = spawn_engine_daemon();

    pthread_mutex_unlock(&g_state_mutex);

    return (g_engine_pid > 0) ? 0 : -1;
}

/* ── Watchdog loop — monitors and restarts the daemon if it exits ─────── */
static void *watchdog_thread(void *arg)
{
    (void)arg;
    AnomalySvcInfo(("Watchdog thread started\n"));

    while (g_running) {
        sleep(ANOMALY_RESTART_DELAY_S);

        pthread_mutex_lock(&g_state_mutex);
        int needs_restart = (g_engine_pid <= 0 ||
                             kill(g_engine_pid, 0) != 0);
        pthread_mutex_unlock(&g_state_mutex);

        if (!g_running) break;

        if (needs_restart) {
            AnomalySvcWarn(("Watchdog: anomaly_app not running; restarting\n"));

            pthread_mutex_lock(&g_state_mutex);
            if (g_engine_pid > 0)
                waitpid(g_engine_pid, NULL, WNOHANG);
            g_engine_pid = spawn_engine_daemon();
            pthread_mutex_unlock(&g_state_mutex);
        }
    }

    AnomalySvcInfo(("Watchdog thread exited\n"));
    return NULL;
}

/* ── main ───────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    daemonize();

    /* ── Logging ── */
    AnomalyService_Log_Init();
    AnomalySvcInfo(("AnomalyService starting\n"));

    /* ── Resolve configurable paths ── */
    const char *env;

    env = getenv("ANOMALY_APP_PATH");
    snprintf(g_anomaly_app_path, sizeof(g_anomaly_app_path),
             "%s", env ? env : ANOMALY_APP_BINARY);

    env = getenv("ANOMALY_CPU_CONFIG_PATH");
    snprintf(g_config_path, sizeof(g_config_path),
             "%s", env ? env : ANOMALY_CPU_CONFIG);

    env = getenv("ANOMALY_MEM_CONFIG_PATH");
    snprintf(g_mem_config_path, sizeof(g_mem_config_path),
             "%s", env ? env : ANOMALY_MEM_CONFIG);

    env = getenv("ANOMALY_WATCH_PATH");
    snprintf(g_watch_path, sizeof(g_watch_path),
             "%s", env ? env : ANOMALY_TELEMETRY_CSV);

    env = getenv("ANOMALY_RESULT_PATH");
    snprintf(g_result_path, sizeof(g_result_path),
             "%s", env ? env : ANOMALY_RESULT_CSV);

    env = getenv("ANOMALY_DELEGATE_PATH");
    snprintf(g_delegate_path, sizeof(g_delegate_path),
             "%s", env ? env : ANOMALY_DELEGATE_PATH);

    env = getenv("ANOMALY_DELEGATE_OPTIONS");
    snprintf(g_delegate_options, sizeof(g_delegate_options),
             "%s", env ? env : ANOMALY_DELEGATE_OPTIONS);

    AnomalySvcInfo(("  binary          : %s\n", g_anomaly_app_path));
    AnomalySvcInfo(("  cpu config (-c) : %s\n", g_config_path));
    AnomalySvcInfo(("  mem config      : %s\n", g_mem_config_path));
    AnomalySvcInfo(("  watch path      : %s\n", g_watch_path));
    AnomalySvcInfo(("  result path     : %s\n", g_result_path));
    AnomalySvcInfo(("  delegate (-d)   : %s\n", g_delegate_path));
    AnomalySvcInfo(("  delegate opts   : %s\n", g_delegate_options));

    /* ── Signal setup ── */
    signal(SIGTERM, sig_handler);
    signal(SIGINT,  sig_handler);
    signal(SIGCHLD, sig_handler);

    /* ── Spawn anomaly_app daemon ── */
    pthread_mutex_lock(&g_state_mutex);
    g_engine_pid = spawn_engine_daemon();
    pthread_mutex_unlock(&g_state_mutex);

    if (g_engine_pid < 0) {
        AnomalySvcError(("Failed to start anomaly_app; continuing without it\n"));
    }

    /* ── rbus registration ── */
    if (AnomalyService_Rbus_Init() != RBUS_ERROR_SUCCESS) {
        AnomalySvcError(("rbus init failed; cannot register TR-181 parameters\n"));
        return 1;
    }

    /* ── Watchdog thread ── */
    pthread_t wd_tid;
    if (pthread_create(&wd_tid, NULL, watchdog_thread, NULL) != 0) {
        AnomalySvcError(("Failed to create watchdog thread: %s\n",
                         strerror(errno)));
    }

    AnomalySvcInfo(("AnomalyService ready\n"));

    /* ── Main loop: idle until shutdown signal ── */
    while (g_running) {
        sleep(1);
    }

    /* ── Shutdown ── */
    AnomalySvcInfo(("AnomalyService shutting down\n"));

    AnomalyService_Rbus_Deinit();

    pthread_join(wd_tid, NULL);

    /* Terminate anomaly_app daemon */
    pthread_mutex_lock(&g_state_mutex);
    if (g_engine_pid > 0) {
        kill(g_engine_pid, SIGTERM);
        waitpid(g_engine_pid, NULL, 0);
        g_engine_pid = -1;
    }
    pthread_mutex_unlock(&g_state_mutex);

    AnomalyService_Log_Deinit();
    return 0;
}
