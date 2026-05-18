/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_llm.c — libcurl-based HTTP client for the Anthropic Messages API
 *
 * Security notes
 * ──────────────
 * • SSL peer verification (CURLOPT_SSL_VERIFYPEER / CURLOPT_SSL_VERIFYHOST)
 *   is always enabled to prevent MITM attacks on the API key transmission.
 * • The API key is set via the Authorization header and is never logged.
 * • The response body is size-capped at AI_DIAG_MAX_LLM_RESP bytes to
 *   prevent memory exhaustion from unexpectedly large responses.
 */

#include "ai_diag_llm.h"
#include "ai_diag_log.h"
#include "ai_diag_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <curl/curl.h>

/* ── libcurl global init (idempotent) ────────────────────────────────────── */

static pthread_once_t  g_curl_init_once = PTHREAD_ONCE_INIT;

static void do_curl_global_init(void)
{
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK)
        AiDiagError("llm: curl_global_init failed: %s", curl_easy_strerror(rc));
    else
        AiDiagInfo("llm: libcurl global init complete (version: %s)",
                   curl_version());
}

void ai_diag_llm_global_init(void)
{
    pthread_once(&g_curl_init_once, do_curl_global_init);
}

void ai_diag_llm_global_cleanup(void)
{
    curl_global_cleanup();
}

/* ── Write callback — accumulate response body ───────────────────────────── */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} ResponseBuf;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t bytes = size * nmemb;
    ResponseBuf *rb = (ResponseBuf *)userdata;

    /* Hard cap: refuse data beyond the limit */
    if (rb->len >= AI_DIAG_MAX_LLM_RESP) {
        AiDiagWarn("llm: response body cap (%d bytes) reached; "
                   "discarding %zu bytes", AI_DIAG_MAX_LLM_RESP, bytes);
        return bytes;   /* "consume" the data to avoid curl error */
    }

    size_t take = bytes;
    if (rb->len + take > (size_t)AI_DIAG_MAX_LLM_RESP)
        take = (size_t)AI_DIAG_MAX_LLM_RESP - rb->len;

    /* Grow buffer on demand */
    if (rb->len + take + 1 > rb->cap) {
        size_t new_cap = rb->cap ? rb->cap * 2 : 4096;
        while (new_cap < rb->len + take + 1)
            new_cap *= 2;
        char *p = realloc(rb->buf, new_cap);
        if (!p) {
            AiDiagError("llm: write_cb realloc failed");
            return 0;   /* signals error to libcurl */
        }
        rb->buf = p;
        rb->cap = new_cap;
    }

    memcpy(rb->buf + rb->len, ptr, take);
    rb->len += take;
    rb->buf[rb->len] = '\0';
    return bytes;
}

/* ── ai_diag_llm_call ────────────────────────────────────────────────────── */

int ai_diag_llm_call(const AiDiagConfig *cfg,
                     const char         *request_body,
                     LlmResponse        *resp)
{
    if (!cfg || !request_body || !resp) {
        if (resp) resp->status = LLM_STATUS_ERROR;
        return -1;
    }

    /* Ensure libcurl is initialised */
    ai_diag_llm_global_init();

    CURL     *curl = NULL;
    CURLcode  rc;
    int       ret  = -1;
    ResponseBuf rb  = {NULL, 0, 0};

    /* Build header list */
    struct curl_slist *headers = NULL;
    char auth_header[320];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: Bearer %s", cfg->api_key);

    char ant_ver_header[64];
    snprintf(ant_ver_header, sizeof(ant_ver_header),
             "anthropic-version: %s", cfg->anthropic_version);

    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, ant_ver_header);

    if (!headers) {
        AiDiagError("llm: curl_slist_append failed");
        goto cleanup;
    }

    curl = curl_easy_init();
    if (!curl) {
        AiDiagError("llm: curl_easy_init failed");
        goto cleanup;
    }

    /* ── Configure the request ────────────────────────────────────────────── */

    /* URL */
    curl_easy_setopt(curl, CURLOPT_URL, cfg->llm_endpoint);

    /* POST with JSON body */
    curl_easy_setopt(curl, CURLOPT_POST,           1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     request_body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,  (long)strlen(request_body));

    /* Headers */
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    /* Response accumulator */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &rb);

    /* Timeouts */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,         (long)cfg->llm_timeout_sec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,  15L);

    /* Security: always verify the server certificate */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,  1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,  2L);

    /* Follow redirects (Azure may redirect on auth) */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,  1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,        5L);

    /* Suppress default progress output */
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,      1L);

    /* ── Execute ──────────────────────────────────────────────────────────── */
    AiDiagInfo("llm: POST %s (body %zu bytes)", cfg->llm_endpoint,
               strlen(request_body));

    rc = curl_easy_perform(curl);

    if (rc != CURLE_OK) {
        AiDiagError("llm: curl_easy_perform failed: %s", curl_easy_strerror(rc));
        resp->status = LLM_STATUS_ERROR;
        goto cleanup;
    }

    /* Check HTTP status */
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    AiDiagInfo("llm: HTTP %ld, response %zu bytes", http_code, rb.len);

    if (http_code < 200 || http_code >= 300) {
        AiDiagError("llm: non-2xx HTTP status %ld; body=%.200s",
                    http_code, rb.buf ? rb.buf : "(empty)");
        resp->status = LLM_STATUS_ERROR;
        goto cleanup;
    }

    if (!rb.buf || !rb.len) {
        AiDiagError("llm: empty response body");
        resp->status = LLM_STATUS_ERROR;
        goto cleanup;
    }

    /* ── Parse response ───────────────────────────────────────────────────── */
    //if (cfg->verbose)
        AiDiagInfo("llm: raw response: %.500s", rb.buf);

    char *text = ai_diag_extract_text(rb.buf);
    if (!text) {
        AiDiagError("llm: failed to extract text from response envelope");
        resp->status = LLM_STATUS_ERROR;
        goto cleanup;
    }

    AiDiagInfo("llm: extracted text: %.300s", text);

    if (ai_diag_parse_response(text, resp) != 0) {
        AiDiagError("llm: failed to parse diagnostic response: %.200s", text);
        free(text);
        resp->status = LLM_STATUS_ERROR;
        goto cleanup;
    }

    free(text);
    ret = 0;   /* success */

cleanup:
    if (curl)    curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);
    free(rb.buf);

    /* Scrub auth header from stack (belt-and-suspenders) */
    memset(auth_header, 0, sizeof(auth_header));

    return ret;
}
