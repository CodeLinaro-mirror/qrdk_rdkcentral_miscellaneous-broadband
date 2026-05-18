/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_cmd.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Secure diagnostic command execution for the AI diagnostic workflow.
 *
 * Security model
 * ──────────────
 * Commands originate from an external LLM and MUST be validated before
 * execution.  The executor enforces a two-level security policy:
 *
 *   1. Allow-list  — the leading command token must appear in a whitelist of
 *                    safe read-only diagnostic utilities.
 *   2. Block-list  — the full command string must not contain any substring
 *                    that indicates a write or destructive operation.
 *
 * Commands that fail either check are rejected and logged; they are never
 * executed.
 *
 * Execution model
 * ───────────────
 * Validated commands are run via popen(cmd, "r") so that shell substitutions
 * such as $(pidof CcspWifiSsp) work correctly.  A non-blocking read loop
 * with select() enforces the configured timeout without using global signals,
 * making the executor safe for multi-threaded use.
 *
 * Output is capped at AI_DIAG_MAX_CMD_OUTPUT bytes; truncated outputs are
 * marked with a "[TRUNCATED]" trailer.
 */

#ifndef AI_DIAG_CMD_H
#define AI_DIAG_CMD_H

#include "ai_diag.h"

/*
 * Validate cmd against the security policy.
 *
 * Returns 1 if the command is permitted.
 * Returns 0 if the command is blocked (reason is logged at WARN level).
 */
int ai_diag_cmd_is_allowed(const char *cmd);

/*
 * Execute a single validated command and capture its combined stdout+stderr.
 *
 *   cmd         – command string (will be re-validated internally)
 *   timeout_sec – maximum wall-clock seconds to wait for output
 *
 * Returns a heap-allocated, null-terminated output string on success.
 * Returns a heap-allocated error message string on failure.
 * The caller is always responsible for free().
 * Never returns NULL.
 */
char *ai_diag_cmd_run(const char *cmd, int timeout_sec);

/*
 * Execute a batch of commands and return their outputs.
 *
 *   cmds        – array of command strings (length n)
 *   outputs     – caller-allocated array of (char*) pointers to fill;
 *                 each element is set to a heap-allocated output string
 *   n           – number of commands
 *   timeout_sec – per-command timeout
 *
 * The caller must free each outputs[i] after use.
 */
void ai_diag_cmd_run_batch(const char * const *cmds,
                           char              **outputs,
                           int                 n,
                           int                 timeout_sec);

/*
 * Execute corrective actions from an analysis_complete LLM response.
 *
 * Only "systemctl restart <service>" commands are permitted; all other
 * action strings are skipped and logged.  Service names must consist of
 * [A-Za-z0-9._-] only.
 *
 * Results are logged at INFO level.  This function does not return a value
 * because a failed corrective action should not abort the diagnostic flow.
 */
void ai_diag_execute_actions(const LlmResponse *resp);

#endif /* AI_DIAG_CMD_H */
