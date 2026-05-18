/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_llm.h
 * ─────────────────────────────────────────────────────────────────────────────
 * HTTP client for the Anthropic Messages API hosted on Azure OpenAI.
 *
 * Uses libcurl for transport.  SSL peer verification is always enabled.
 * The API key is taken exclusively from AiDiagConfig::api_key and is never
 * written to any log output.
 */

#ifndef AI_DIAG_LLM_H
#define AI_DIAG_LLM_H

#include "ai_diag.h"
#include "ai_diag_config.h"
#include "ai_diag_json.h"    /* LlmResponse */

/*
 * Send a single-turn request to the LLM and parse the response.
 *
 *   cfg          – active configuration (endpoint, model, key, timeouts)
 *   request_body – complete JSON request string (from ai_diag_build_request)
 *   resp         – output: populated on success
 *
 * Returns  0 on success (resp->status is LLM_STATUS_NEED_DATA or
 *                         LLM_STATUS_ANALYSIS_COMPLETE).
 * Returns -1 on transport error, timeout, or parse failure
 *            (resp->status is set to LLM_STATUS_ERROR).
 *
 * All network and parse errors are logged internally.
 */
int ai_diag_llm_call(const AiDiagConfig *cfg,
                     const char         *request_body,
                     LlmResponse        *resp);

/*
 * Initialise the libcurl global state.
 * Must be called once at process start (before spawning threads).
 * Idempotent; safe to call multiple times.
 */
void ai_diag_llm_global_init(void);

/*
 * Clean up the libcurl global state.
 * Call once at process exit.
 */
void ai_diag_llm_global_cleanup(void);

#endif /* AI_DIAG_LLM_H */
