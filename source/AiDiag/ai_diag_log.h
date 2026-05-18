/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_log.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Thread-safe file-based logging for the AI diagnostic service.
 *
 * Log output format:
 *   YYYY-MM-DD HH:MM:SS [AI_DIAG][LEVEL] <message>
 *
 * The log file path defaults to AI_DIAG_LOG_PATH (/rdklogs/logs/ai_diag.txt)
 * and can be overridden before the first log call via ai_diag_log_init().
 */

#ifndef AI_DIAG_LOG_H
#define AI_DIAG_LOG_H

#ifndef AI_DIAG_LOG_PATH
#define AI_DIAG_LOG_PATH  "/rdklogs/logs/ai_diag.txt"
#endif

/*
 * Initialise (or re-configure) the log subsystem.
 * Call once at startup with the desired log file path.
 * May be called multiple times; each call updates the path atomically.
 * If log_path is NULL or empty, the default compile-time path is used.
 */
void ai_diag_log_init(const char *log_path);

/*
 * Core log function — write a timestamped line to the log file.
 * Falls back to stderr if the log file cannot be opened.
 * Thread-safe: protected by an internal mutex.
 */
void ai_diag_log_write(const char *level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* ── Convenience macros ──────────────────────────────────────────────────── */
#define AiDiagError(fmt, ...)  ai_diag_log_write("ERROR", fmt, ##__VA_ARGS__)
#define AiDiagWarn(fmt, ...)   ai_diag_log_write("WARN",  fmt, ##__VA_ARGS__)
#define AiDiagInfo(fmt, ...)   ai_diag_log_write("INFO",  fmt, ##__VA_ARGS__)
#define AiDiagDebug(fmt, ...)  ai_diag_log_write("DEBUG", fmt, ##__VA_ARGS__)

#endif /* AI_DIAG_LOG_H */
