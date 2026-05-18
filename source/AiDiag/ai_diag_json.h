/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_json.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Minimal JSON helpers for building LLM request bodies and parsing diagnostic
 * responses.  No external JSON library is required — all parsing is done with
 * targeted string scanning against the well-known response schema.
 *
 * Response schema (inner text extracted from the HTTP envelope):
 *
 *   need_data:
 *     {"status":"need_data","next_commands":["cmd1","cmd2",...]}
 *
 *   analysis_complete:
 *     {"status":"analysis_complete","suspected_issue":"...","confidence":"high",
 *      "recommended_action":["action1","action2",...]}
 */

#ifndef AI_DIAG_JSON_H
#define AI_DIAG_JSON_H

#include "ai_diag.h"
#include "ai_diag_config.h"
#include <stddef.h>

/* ── Dynamic string buffer ───────────────────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} dynstr_t;

void  dynstr_init(dynstr_t *ds);
int   dynstr_append(dynstr_t *ds, const char *str);
int   dynstr_appendf(dynstr_t *ds, const char *fmt, ...)
          __attribute__((format(printf, 2, 3)));
/* Append str JSON-escaped and surrounded by double-quotes. */
int   dynstr_append_json_str(dynstr_t *ds, const char *str);
/* Transfer ownership of the buffer to caller; ds is reset to empty. */
char *dynstr_steal(dynstr_t *ds);
void  dynstr_free(dynstr_t *ds);

/* ── JSON string escaping ─────────────────────────────────────────────────── */

/*
 * Write src into dst as a JSON-escaped string (WITHOUT surrounding quotes).
 * Returns the number of bytes written (not counting the null terminator).
 * dst_cap must be at least 1 (for the null terminator).
 */
int json_escape(const char *src, char *dst, size_t dst_cap);

/* ── LLM request builder ─────────────────────────────────────────────────── */

/*
 * Build a complete JSON request body for the Anthropic messages API.
 *
 *   cfg           – active configuration (model, tokens, temperature)
 *   system_prompt – static system instruction string
 *   user_content  – single user message content (plain text or JSON string)
 *
 * Returns a heap-allocated, null-terminated JSON string.
 * Caller is responsible for free().
 * Returns NULL on allocation failure.
 */
char *ai_diag_build_request(const AiDiagConfig *cfg,
                            const char         *system_prompt,
                            const char         *user_content);

/* ── LLM response parsing ─────────────────────────────────────────────────── */

/*
 * Extract the diagnostic text from the raw HTTP response envelope.
 *
 * The envelope format is:
 *   {"model":"...","content":[{"type":"text","text":"<escaped_json>"}],...}
 *
 * Returns a heap-allocated, null-terminated, unescaped JSON string
 * (the value of content[0].text).  Caller must free().
 * Returns NULL if the expected structure is not found.
 */
char *ai_diag_extract_text(const char *response_body);

/*
 * Parse the inner diagnostic JSON text into an LlmResponse struct.
 *
 * Returns  0 on success (resp->status is set appropriately).
 * Returns -1 on parse error (resp->status is set to LLM_STATUS_ERROR).
 */
int ai_diag_parse_response(const char *text, LlmResponse *resp);

/* ── User message content builders ──────────────────────────────────────────*/

/*
 * Build the content string for turn 1: initial anomaly notification.
 * Returns a heap-allocated string.  Caller must free().
 */
char *ai_diag_build_initial_content(const AnomalyEvent *event);

/*
 * Build the content string for the forced-final turn.
 * Used when max_turns is reached to prompt a best-effort analysis_complete.
 * Returns a heap-allocated string.  Caller must free().
 */
char *ai_diag_build_forced_final_content(const AnomalyEvent *event);

/*
 * Build the content string for turns 2-N: command output feedback.
 *
 *   event   – the original anomaly event (for context labelling)
 *   turn    – current turn number (2-based)
 *   cmds    – array of command strings that were executed
 *   outputs – array of corresponding captured outputs
 *   n       – number of commands/outputs
 *
 * Returns a heap-allocated JSON string.  Caller must free().
 */
char *ai_diag_build_followup_content(const AnomalyEvent *event,
                                     int                 turn,
                                     const char * const *cmds,
                                     const char * const *outputs,
                                     int                 n);

#endif /* AI_DIAG_JSON_H */
