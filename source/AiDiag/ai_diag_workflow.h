/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_workflow.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Iterative LLM-driven diagnostic workflow.
 *
 * The workflow implements the request-response loop from example.txt:
 *
 *   Turn 1 ──► LLM ──► need_data: run next_commands ──► Turn 2 ──► ...
 *                └──► analysis_complete: log result, done
 *
 * Each turn is a fresh, stateless API call (no growing conversation history)
 * with the same comprehensive system prompt providing all context.
 * The loop continues until either the LLM returns analysis_complete or
 * the maximum turn count (cfg->max_turns) is reached.
 */

#ifndef AI_DIAG_WORKFLOW_H
#define AI_DIAG_WORKFLOW_H

#include "ai_diag.h"
#include "ai_diag_config.h"

/*
 * Arguments for a workflow invocation.
 * The struct is heap-allocated by the caller before thread creation and
 * freed by ai_diag_workflow_run() when the workflow ends.
 */
typedef struct {
    AnomalyEvent event;
    AiDiagConfig cfg;
} WorkflowArgs;

/*
 * Run the full diagnostic workflow for one anomaly event.
 *
 * This function is designed to be used as a pthread_create entry point:
 *   pthread_create(&tid, NULL, ai_diag_workflow_thread, args);
 *
 * args must be a heap-allocated WorkflowArgs*.
 * The function always frees args before returning.
 * Always returns NULL.
 */
void *ai_diag_workflow_thread(void *args);

/*
 * Synchronous entry point (for testing or single-threaded use).
 * Equivalent to ai_diag_workflow_thread but does not free args.
 *
 * Returns  0 if analysis_complete was reached.
 * Returns -1 on error or if max turns were exhausted without a conclusion.
 */
int ai_diag_workflow_run(const AnomalyEvent *event, const AiDiagConfig *cfg);

#endif /* AI_DIAG_WORKFLOW_H */
