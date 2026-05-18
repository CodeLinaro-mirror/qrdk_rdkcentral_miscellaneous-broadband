/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_json.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Minimal JSON building and parsing for the AI diagnostic workflow.
 *
 * Parsing strategy: targeted string scanning against the well-known Anthropic
 * response schema.  This avoids pulling in a general-purpose JSON library on
 * the constrained embedded target.
 */

#include "ai_diag_json.h"
#include "ai_diag_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── dynstr_t implementation ─────────────────────────────────────────────── */

#define DYNSTR_INIT_CAP 1024

void dynstr_init(dynstr_t *ds)
{
    ds->buf = NULL;
    ds->len = 0;
    ds->cap = 0;
}

/* Grow the buffer to hold at least need bytes total. */
static int dynstr_grow(dynstr_t *ds, size_t need)
{
    if (need <= ds->cap)
        return 0;

    size_t new_cap = ds->cap ? ds->cap * 2 : DYNSTR_INIT_CAP;
    while (new_cap < need)
        new_cap *= 2;

    char *p = realloc(ds->buf, new_cap);
    if (!p)
        return -1;

    ds->buf = p;
    ds->cap = new_cap;
    return 0;
}

int dynstr_append(dynstr_t *ds, const char *str)
{
    if (!str)
        return 0;

    size_t slen = strlen(str);
    if (dynstr_grow(ds, ds->len + slen + 1) != 0)
        return -1;

    memcpy(ds->buf + ds->len, str, slen);
    ds->len += slen;
    ds->buf[ds->len] = '\0';
    return 0;
}

int dynstr_appendf(dynstr_t *ds, const char *fmt, ...)
{
    /* Format into a 4 KiB stack buffer; sufficient for all internal callers. */
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0)
        return -1;
    if ((size_t)n >= sizeof(tmp)) {
        /* Truncate rather than allocate dynamically for this internal path. */
        tmp[sizeof(tmp) - 1] = '\0';
    }
    return dynstr_append(ds, tmp);
}

int dynstr_append_json_str(dynstr_t *ds, const char *str)
{
    /* Upper-bound for the escaped length: each byte can expand to at most 6
     * chars (\uXXXX), plus 2 surrounding quotes and a null terminator. */
    size_t src_len  = str ? strlen(str) : 0;
    size_t esc_cap  = src_len * 6 + 3;   /* 2 quotes + NUL */
    char  *esc_buf  = malloc(esc_cap);
    if (!esc_buf)
        return -1;

    esc_buf[0] = '"';
    int n = json_escape(str ? str : "", esc_buf + 1, esc_cap - 2);
    esc_buf[1 + n] = '"';
    esc_buf[2 + n] = '\0';

    int rc = dynstr_append(ds, esc_buf);
    free(esc_buf);
    return rc;
}

char *dynstr_steal(dynstr_t *ds)
{
    char *p = ds->buf;
    if (!p) {
        /* Return an empty heap string so callers don't have to NULL-check. */
        p = malloc(1);
        if (p) p[0] = '\0';
    }
    ds->buf = NULL;
    ds->len = 0;
    ds->cap = 0;
    return p;
}

void dynstr_free(dynstr_t *ds)
{
    free(ds->buf);
    dynstr_init(ds);
}

/* ── json_escape ─────────────────────────────────────────────────────────── */

int json_escape(const char *src, char *dst, size_t dst_cap)
{
    size_t i = 0;
    if (!src || !dst || dst_cap < 1)
        return 0;

    while (*src) {
        unsigned char c = (unsigned char)*src;

        if (c == '"' || c == '\\') {
            if (i + 2 >= dst_cap) break;
            dst[i++] = '\\';
            dst[i++] = (char)c;
        } else if (c == '\n') {
            if (i + 2 >= dst_cap) break;
            dst[i++] = '\\'; dst[i++] = 'n';
        } else if (c == '\r') {
            if (i + 2 >= dst_cap) break;
            dst[i++] = '\\'; dst[i++] = 'r';
        } else if (c == '\t') {
            if (i + 2 >= dst_cap) break;
            dst[i++] = '\\'; dst[i++] = 't';
        } else if (c < 0x20) {
            /* Other control characters: encode as \uXXXX */
            if (i + 6 >= dst_cap) break;
            snprintf(dst + i, 7, "\\u%04x", c);
            i += 6;
        } else {
            if (i + 1 >= dst_cap) break;
            dst[i++] = (char)c;
        }
        src++;
    }
    dst[i] = '\0';
    return (int)i;
}

/* ── Static helpers for JSON parsing ─────────────────────────────────────── */

/*
 * Find "key":"value" in json and copy the unescaped value into buf.
 * Returns 1 if found, 0 otherwise.
 */
static int jstr(const char *json, const char *key, char *buf, size_t buf_sz)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);

    const char *p = strstr(json, pattern);
    if (!p)
        return 0;
    p += strlen(pattern);

    size_t i = 0;
    while (*p && *p != '"' && i < buf_sz - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case '"':  buf[i++] = '"';  break;
                case '\\': buf[i++] = '\\'; break;
                case 'n':  buf[i++] = '\n'; break;
                case 'r':  buf[i++] = '\r'; break;
                case 't':  buf[i++] = '\t'; break;
                default:   buf[i++] = *p;   break;
            }
        } else {
            buf[i++] = *p;
        }
        p++;
    }
    buf[i] = '\0';
    return 1;
}

/*
 * Find "key":[...] in json and extract up to max_items quoted string values
 * into a flat array of fixed-width slots (arr[i] has item_size bytes).
 * Returns the number of items extracted.
 */
static int jarr(const char *json, const char *key,
                char *arr, size_t item_size, int max_items)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":[", key);

    const char *p = strstr(json, pattern);
    if (!p)
        return 0;
    p += strlen(pattern);

    int count = 0;
    while (*p && *p != ']' && count < max_items) {
        /* Skip whitespace and commas */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')
            p++;

        if (*p == '"') {
            p++;   /* skip opening quote */
            char *dst = arr + (size_t)count * item_size;
            size_t i = 0;
            while (*p && *p != '"' && i < item_size - 1) {
                if (*p == '\\' && *(p + 1)) {
                    p++;
                    switch (*p) {
                        case '"':  dst[i++] = '"';  break;
                        case '\\': dst[i++] = '\\'; break;
                        case 'n':  dst[i++] = '\n'; break;
                        case 'r':  dst[i++] = '\r'; break;
                        case 't':  dst[i++] = '\t'; break;
                        default:   dst[i++] = *p;   break;
                    }
                } else {
                    dst[i++] = *p;
                }
                p++;
            }
            dst[i] = '\0';
            count++;
            if (*p == '"')
                p++;   /* skip closing quote */
        } else if (*p != ']') {
            p++;   /* skip unexpected token */
        }
    }
    return count;
}

/* ── ai_diag_build_request ───────────────────────────────────────────────── */

char *ai_diag_build_request(const AiDiagConfig *cfg,
                            const char         *system_prompt,
                            const char         *user_content)
{
    dynstr_t ds;
    dynstr_init(&ds);

    dynstr_appendf(&ds,
        "{"
        "\"model\":\"%s\","
        "\"max_tokens\":%d,"
        "\"temperature\":%.2f,"
        "\"system\":",
        cfg->llm_model,
        cfg->max_tokens,
        (double)cfg->temperature);

    dynstr_append_json_str(&ds, system_prompt);

    dynstr_append(&ds, ",\"messages\":[{\"role\":\"user\",\"content\":");
    dynstr_append_json_str(&ds, user_content);
    dynstr_append(&ds, "}]}");

    return dynstr_steal(&ds);
}

/* ── ai_diag_extract_text ────────────────────────────────────────────────── */

char *ai_diag_extract_text(const char *response_body)
{
    if (!response_body)
        return NULL;

    /* Locate "content": array first, then find "text":"..." within it.
     * This avoids false matches on "text" keys elsewhere in the envelope. */
    const char *content_start = strstr(response_body, "\"content\":");
    if (!content_start) {
        AiDiagWarn("extract_text: 'content' key not found in response");
        return NULL;
    }

    const char *text_marker = strstr(content_start, "\"text\":\"");
    if (!text_marker) {
        AiDiagWarn("extract_text: 'text' key not found in content array");
        return NULL;
    }
    text_marker += strlen("\"text\":\"");

    /* Read the escaped string value until the unescaped closing quote. */
    dynstr_t ds;
    dynstr_init(&ds);

    const char *p = text_marker;
    while (*p) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            char tmp[2] = { *p, '\0' };
            switch (*p) {
                case '"':  tmp[0] = '"';  break;
                case '\\': tmp[0] = '\\'; break;
                case 'n':  tmp[0] = '\n'; break;
                case 'r':  tmp[0] = '\r'; break;
                case 't':  tmp[0] = '\t'; break;
                default:   tmp[0] = *p;   break;
            }
            dynstr_append(&ds, tmp);
        } else if (*p == '"') {
            break;   /* end of text value */
        } else {
            char tmp[2] = { *p, '\0' };
            dynstr_append(&ds, tmp);
        }
        p++;
    }

    char *result = dynstr_steal(&ds);
    if (!result || !*result) {
        free(result);
        AiDiagWarn("extract_text: empty text value extracted");
        return NULL;
    }
    return result;
}

/* ── ai_diag_parse_response ──────────────────────────────────────────────── */

int ai_diag_parse_response(const char *text, LlmResponse *resp)
{
    if (!text || !resp) {
        if (resp) resp->status = LLM_STATUS_ERROR;
        return -1;
    }

    memset(resp, 0, sizeof(*resp));
    resp->status = LLM_STATUS_ERROR;

    char status_str[64] = {0};
    if (!jstr(text, "status", status_str, sizeof(status_str))) {
        AiDiagWarn("parse_response: 'status' field not found in: %.120s", text);
        return -1;
    }

    if (strcmp(status_str, "need_data") == 0) {
        resp->status = LLM_STATUS_NEED_DATA;
        resp->num_commands = jarr(text, "next_commands",
                                  &resp->next_commands[0][0],
                                  AI_DIAG_MAX_CMD_LEN,
                                  AI_DIAG_MAX_COMMANDS);
        if (resp->num_commands == 0)
            AiDiagWarn("parse_response: need_data but no next_commands found");

    } else if (strcmp(status_str, "analysis_complete") == 0) {
        resp->status = LLM_STATUS_ANALYSIS_COMPLETE;
        jstr(text, "suspected_issue", resp->suspected_issue,
             sizeof(resp->suspected_issue));
        jstr(text, "confidence",      resp->confidence,
             sizeof(resp->confidence));
        resp->num_actions = jarr(text, "recommended_action",
                                 &resp->recommended_action[0][0],
                                 sizeof(resp->recommended_action[0]),
                                 AI_DIAG_MAX_ACTIONS);
    } else {
        AiDiagWarn("parse_response: unknown status '%s'", status_str);
        return -1;
    }

    resp->status = (strcmp(status_str, "analysis_complete") == 0)
                   ? LLM_STATUS_ANALYSIS_COMPLETE
                   : LLM_STATUS_NEED_DATA;
    return 0;
}

/* ── ai_diag_build_initial_content ──────────────────────────────────────── */

char *ai_diag_build_initial_content(const AnomalyEvent *event)
{
    dynstr_t ds;
    dynstr_init(&ds);

    const char *label = ai_anomaly_label(event->anomaly_type);

    /* Format matches the style seen in example.txt turn-1 content, extended
     * with key metrics so the LLM has quantitative context from the start. */
    dynstr_appendf(&ds,
        "%s detected in RDKB device. "
        "MAC: %s. "
        "CPU_MSE=%.6f CPU_SEV=%.2fx MEM_MSE=%.6f MEM_SEV=%.2fx. "
        "Timestamp: %s.",
        label,
        event->mac,
        (double)event->cpu_mse,
        (double)event->cpu_sev,
        (double)event->mem_mse,
        (double)event->mem_sev,
        event->timestamp);

    return dynstr_steal(&ds);
}

/* ── ai_diag_build_forced_final_content ─────────────────────────────────── */

char *ai_diag_build_forced_final_content(const AnomalyEvent *event)
{
    dynstr_t ds;
    dynstr_init(&ds);

    dynstr_appendf(&ds,
        "Maximum diagnostic turns reached. "
        "Anomaly: %s for MAC %s "
        "(CPU_SEV=%.2fx MEM_SEV=%.2fx Timestamp: %s). "
        "Provide your best-effort analysis now. "
        "Do NOT request more commands.",
        ai_anomaly_label(event->anomaly_type),
        event->mac,
        (double)event->cpu_sev,
        (double)event->mem_sev,
        event->timestamp);

    return dynstr_steal(&ds);
}

/* ── ai_diag_build_followup_content ─────────────────────────────────────── */

char *ai_diag_build_followup_content(const AnomalyEvent *event,
                                     int                 turn,
                                     const char * const *cmds,
                                     const char * const *outputs,
                                     int                 n)
{
    dynstr_t ds;
    dynstr_init(&ds);

    const char *label = ai_anomaly_label(event->anomaly_type);

    /* Build the outer envelope */
    dynstr_appendf(&ds,
        "{\"event\":\"%s_followup\",\"turn\":%d,\"mac\":\"%s\","
        "\"commands_executed\":[",
        label, turn, event->mac);

    for (int i = 0; i < n; i++) {
        if (i > 0)
            dynstr_append(&ds, ",");
        dynstr_append(&ds, "{\"cmd\":");
        dynstr_append_json_str(&ds, cmds[i] ? cmds[i] : "");
        dynstr_append(&ds, ",\"output\":");
        dynstr_append_json_str(&ds, outputs[i] ? outputs[i] : "(no output)");
        dynstr_append(&ds, "}");
    }

    dynstr_append(&ds, "]}");

    return dynstr_steal(&ds);
}
