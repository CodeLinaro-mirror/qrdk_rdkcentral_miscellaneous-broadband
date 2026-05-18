/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_config.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Runtime configuration for the AI diagnostic daemon.
 *
 * Configuration is loaded in priority order:
 *   1. Environment variables (highest priority)
 *   2. Compile-time defaults (lowest priority)
 *
 * The API key MUST be supplied via the AZURE_API_KEY environment variable.
 * It is never stored in any file or logged.
 */

#ifndef AI_DIAG_CONFIG_H
#define AI_DIAG_CONFIG_H

/* ── Default paths and values ─────────────────────────────────────────────── */
#define AI_DIAG_DEFAULT_WATCH_PATH    "/rdklogs/logs/anomaly_results.csv"
#define AI_DIAG_DEFAULT_LLM_ENDPOINT  \
    "https://rdkb-dev-resource.openai.azure.com/anthropic/v1/messages"
#define AI_DIAG_DEFAULT_LLM_MODEL     "claude-opus-4-6"
#define AI_DIAG_DEFAULT_ANTHROPIC_VER "2023-06-01"
#define AI_DIAG_DEFAULT_MAX_TURNS     5
#define AI_DIAG_DEFAULT_CMD_TIMEOUT   30    /* seconds per diagnostic command  */
#define AI_DIAG_DEFAULT_LLM_TIMEOUT   90    /* seconds per LLM HTTP call       */
#define AI_DIAG_DEFAULT_POLL_MS       5000  /* file-watcher poll interval (ms) */
#define AI_DIAG_DEFAULT_COOLDOWN_SEC  300   /* min seconds between diagnostics
                                               for the same MAC address        */
#define AI_DIAG_DEFAULT_MAX_TOKENS    1000
#define AI_DIAG_DEFAULT_TEMPERATURE   0.7f

/* ── Configuration structure ─────────────────────────────────────────────── */
typedef struct {
    /* LLM endpoint settings */
    char  llm_endpoint[512];
    char  llm_model[64];
    char  api_key[256];         /* populated from AZURE_API_KEY env var ONLY  */
    char  anthropic_version[32];
    int   max_tokens;
    float temperature;

    /* Workflow limits */
    int   max_turns;
    int   cmd_timeout_sec;
    int   llm_timeout_sec;
    int   cooldown_sec;

    /* File-watcher settings */
    char  watch_path[512];
    int   poll_interval_ms;
    int   skip_existing;        /* 1 = skip CSV rows already present at boot  */

    /* Logging */
    char  log_path[256];
    int   verbose;

    /* Daemon mode */
    int   daemonize;
} AiDiagConfig;

/*
 * Load configuration.
 *
 * Fills *cfg with compile-time defaults, then overrides with any environment
 * variables that are set.  config_file is reserved for future use (pass NULL).
 *
 * Returns  0 on success.
 * Returns -1 if AZURE_API_KEY is not set (LLM calls will fail).
 */
int  ai_diag_config_load(AiDiagConfig *cfg, const char *config_file);

/*
 * Log a summary of the active configuration (API key is redacted).
 */
void ai_diag_config_dump(const AiDiagConfig *cfg);

#endif /* AI_DIAG_CONFIG_H */
