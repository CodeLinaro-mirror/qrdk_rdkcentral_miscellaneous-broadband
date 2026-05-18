/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_config.c — configuration loading from environment variables
 */

#include "ai_diag_config.h"
#include "ai_diag_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Apply compile-time defaults ─────────────────────────────────────────── */
static void set_defaults(AiDiagConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->llm_endpoint,      AI_DIAG_DEFAULT_LLM_ENDPOINT,
            sizeof(cfg->llm_endpoint)      - 1);
    strncpy(cfg->llm_model,         AI_DIAG_DEFAULT_LLM_MODEL,
            sizeof(cfg->llm_model)         - 1);
    strncpy(cfg->anthropic_version, AI_DIAG_DEFAULT_ANTHROPIC_VER,
            sizeof(cfg->anthropic_version) - 1);
    strncpy(cfg->watch_path,        AI_DIAG_DEFAULT_WATCH_PATH,
            sizeof(cfg->watch_path)        - 1);
    strncpy(cfg->log_path,          AI_DIAG_LOG_PATH,
            sizeof(cfg->log_path)          - 1);

    cfg->max_tokens        = AI_DIAG_DEFAULT_MAX_TOKENS;
    cfg->temperature       = AI_DIAG_DEFAULT_TEMPERATURE;
    cfg->max_turns         = AI_DIAG_DEFAULT_MAX_TURNS;
    cfg->cmd_timeout_sec   = AI_DIAG_DEFAULT_CMD_TIMEOUT;
    cfg->llm_timeout_sec   = AI_DIAG_DEFAULT_LLM_TIMEOUT;
    cfg->cooldown_sec      = AI_DIAG_DEFAULT_COOLDOWN_SEC;
    cfg->poll_interval_ms  = AI_DIAG_DEFAULT_POLL_MS;
    cfg->skip_existing     = 1;
    cfg->verbose           = 0;
    cfg->daemonize         = 0;
}

/* ── Safe string copy from environment variable ───────────────────────────── */
static void env_str(const char *name, char *dst, size_t dst_sz)
{
    const char *v = getenv(name);
    if (v && *v) {
        strncpy(dst, v, dst_sz - 1);
        dst[dst_sz - 1] = '\0';
    }
}

/* ── Safe integer read from environment variable ─────────────────────────── */
static void env_int(const char *name, int *dst)
{
    const char *v = getenv(name);
    if (v && *v)
        *dst = atoi(v);
}

/* ── Override defaults with environment variables ─────────────────────────── */
static void load_env(AiDiagConfig *cfg)
{
    /* Security: API key from environment only, never from a config file. */
    env_str("AZURE_API_KEY",          cfg->api_key,          sizeof(cfg->api_key));
    env_str("AI_DIAG_LLM_ENDPOINT",   cfg->llm_endpoint,     sizeof(cfg->llm_endpoint));
    env_str("AI_DIAG_LLM_MODEL",      cfg->llm_model,        sizeof(cfg->llm_model));
    env_str("AI_DIAG_WATCH_PATH",     cfg->watch_path,       sizeof(cfg->watch_path));
    env_str("AI_DIAG_LOG_PATH",       cfg->log_path,         sizeof(cfg->log_path));
    env_str("AI_DIAG_ANTHROPIC_VER",  cfg->anthropic_version,sizeof(cfg->anthropic_version));

    env_int("AI_DIAG_MAX_TURNS",      &cfg->max_turns);
    env_int("AI_DIAG_CMD_TIMEOUT",    &cfg->cmd_timeout_sec);
    env_int("AI_DIAG_LLM_TIMEOUT",    &cfg->llm_timeout_sec);
    env_int("AI_DIAG_POLL_MS",        &cfg->poll_interval_ms);
    env_int("AI_DIAG_COOLDOWN",       &cfg->cooldown_sec);
    env_int("AI_DIAG_MAX_TOKENS",     &cfg->max_tokens);
    env_int("AI_DIAG_VERBOSE",        &cfg->verbose);
    env_int("AI_DIAG_DAEMONIZE",      &cfg->daemonize);
    env_int("AI_DIAG_SKIP_EXISTING",  &cfg->skip_existing);

    /* temperature: read as float from string */
    const char *tv = getenv("AI_DIAG_TEMPERATURE");
    if (tv && *tv)
        cfg->temperature = (float)atof(tv);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int ai_diag_config_load(AiDiagConfig *cfg, const char *config_file)
{
    (void)config_file;   /* reserved for future JSON config file support */

    set_defaults(cfg);
    load_env(cfg);

    /* Validate mandatory field */
    if (!cfg->api_key[0]) {
        AiDiagWarn("AZURE_API_KEY environment variable is not set; "
                   "LLM calls will fail");
        return -1;
    }

    /* Clamp limits to sane ranges */
    if (cfg->max_turns      < 1)  cfg->max_turns      = 1;
    if (cfg->max_turns      > 10) cfg->max_turns      = 10;
    if (cfg->cmd_timeout_sec < 5) cfg->cmd_timeout_sec = 5;
    if (cfg->llm_timeout_sec < 10) cfg->llm_timeout_sec = 10;
    if (cfg->poll_interval_ms < 1000) cfg->poll_interval_ms = 1000;
    if (cfg->max_tokens < 100)    cfg->max_tokens = 100;
    if (cfg->max_tokens > 4096)   cfg->max_tokens = 4096;

    return 0;
}

void ai_diag_config_dump(const AiDiagConfig *cfg)
{
    AiDiagInfo("Config: endpoint=%s model=%s anthropic_ver=%s",
               cfg->llm_endpoint, cfg->llm_model, cfg->anthropic_version);
    AiDiagInfo("Config: max_turns=%d cmd_timeout=%ds llm_timeout=%ds max_tokens=%d",
               cfg->max_turns, cfg->cmd_timeout_sec,
               cfg->llm_timeout_sec, cfg->max_tokens);
    AiDiagInfo("Config: watch_path=%s poll_ms=%d cooldown=%ds skip_existing=%d",
               cfg->watch_path, cfg->poll_interval_ms,
               cfg->cooldown_sec, cfg->skip_existing);
    AiDiagInfo("Config: log_path=%s verbose=%d daemonize=%d",
               cfg->log_path, cfg->verbose, cfg->daemonize);
    /* API key: log only whether it is set, never its value */
    AiDiagInfo("Config: api_key=%s", cfg->api_key[0] ? "<set>" : "<NOT SET>");
}
