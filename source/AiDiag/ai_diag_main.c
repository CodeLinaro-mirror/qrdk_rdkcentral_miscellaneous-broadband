/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_main.c
 * ─────────────────────────────────────────────────────────────────────────────
 * Entry point for the AI diagnostic daemon.
 *
 * The daemon watches /rdklogs/logs/anomaly_results.csv (or a path configured
 * via AI_DIAG_WATCH_PATH) for rows appended by the anomaly detection
 * application.  For each non-Normal row it spawns a detached workflow thread
 * that drives the iterative LLM diagnostic loop.
 *
 * File watching
 * ─────────────
 * Linux: inotify IN_MODIFY + IN_CLOSE_WRITE on the target file.
 *         A stat-based fallback covers the case where the file is replaced
 *         (rotated) while the daemon is running.
 * Non-Linux: stat-polling at cfg.poll_interval_ms.
 *
 * Per-MAC deduplication
 * ─────────────────────
 * At most one workflow runs per MAC address at a time.  A cooldown period
 * (cfg.cooldown_sec, default 5 minutes) prevents repeated diagnostics from
 * a flapping device.
 *
 * CSV format (anomaly_results.csv)
 * ────────────────────────────────
 * Header row:
 *   timestamp,CMMAC,dense_cpu_mse,dense_mem_mse,dense_cpu_flag,
 *   dense_mem_flag,dense_cpu_sev,dense_mem_sev,anomaly_type
 *
 * anomaly_type values: Normal | CPU | Memory | Both
 *
 * Usage
 * ─────
 *   AiDiag [options]
 *
 *   Options:
 *     -w <path>   watch path  (default: AI_DIAG_DEFAULT_WATCH_PATH)
 *     -l <path>   log path    (default: AI_DIAG_LOG_PATH)
 *     -d          daemonize
 *     -s          skip rows already in the CSV at startup
 *     -v          verbose (log LLM request bodies)
 *     -h          print this help and exit
 *
 *   All options can also be set via environment variables (see ai_diag_config.h).
 */

#define _GNU_SOURCE 1

#include "ai_diag.h"
#include "ai_diag_config.h"
#include "ai_diag_log.h"
#include "ai_diag_llm.h"
#include "ai_diag_workflow.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <limits.h>
#include <poll.h>
#include <sys/inotify.h>
#endif

/* ── Signal flag ─────────────────────────────────────────────────────────── */

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* ── Per-MAC active workflow tracking ────────────────────────────────────── */

typedef struct {
    char   mac[AI_DIAG_MAC_LEN];
    time_t last_trigger;
    int    active;          /* non-zero while a workflow thread is running */
} MacEntry;

static MacEntry        g_macs[AI_DIAG_MAX_TRACKED_MACS];
static int             g_mac_count = 0;
static pthread_mutex_t g_mac_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Find or create an entry for mac. Returns NULL if table is full. */
static MacEntry *mac_entry(const char *mac)
{
    for (int i = 0; i < g_mac_count; i++)
        if (strcmp(g_macs[i].mac, mac) == 0)
            return &g_macs[i];

    if (g_mac_count >= AI_DIAG_MAX_TRACKED_MACS)
        return NULL;

    MacEntry *e = &g_macs[g_mac_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->mac, mac, AI_DIAG_MAC_LEN - 1);
    return e;
}

/* ── Workflow thread wrapper that clears the active flag when done ─────────── */

typedef struct {
    WorkflowArgs *wa;
    MacEntry     *entry;   /* pointer into g_macs — valid for process lifetime */
} ThreadCtx;

static void *workflow_wrapper(void *arg)
{
    ThreadCtx *ctx = (ThreadCtx *)arg;
    WorkflowArgs *wa    = ctx->wa;
    MacEntry     *entry = ctx->entry;
    free(ctx);

    /* Delegate to the workflow engine (frees wa internally) */
    ai_diag_workflow_thread(wa);

    /* Mark the slot as no longer active */
    pthread_mutex_lock(&g_mac_mutex);
    if (entry)
        entry->active = 0;
    pthread_mutex_unlock(&g_mac_mutex);

    return NULL;
}

/* ── CSV helpers ─────────────────────────────────────────────────────────── */

/* Split a comma-separated line into at most max_tok tokens.
 * Returns the number of tokens found. */
static int split_csv(const char *line, char **toks, int max_tok)
{
    static char buf[2048];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int n = 0;
    char *p = buf;
    while (n < max_tok) {
        toks[n++] = p;
        p = strchr(p, ',');
        if (!p) break;
        *p++ = '\0';
    }
    return n;
}

/* Trim leading/trailing whitespace and quotes in-place. */
static void trim(char *s)
{
    if (!s) return;
    /* leading */
    size_t i = 0;
    while (s[i] && (isspace((unsigned char)s[i]) || s[i] == '"')) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    /* trailing */
    size_t n = strlen(s);
    while (n && (isspace((unsigned char)s[n-1]) || s[n-1] == '"')) s[--n] = '\0';
}

/* Parse anomaly_type string → AiAnomalyType */
static AiAnomalyType parse_anomaly_type(const char *s)
{
    if (!s) return AI_ANOMALY_UNKNOWN;
    if (strcmp(s, "CPU")    == 0) return AI_ANOMALY_CPU;
    if (strcmp(s, "Memory") == 0) return AI_ANOMALY_MEMORY;
    if (strcmp(s, "Both")   == 0) return AI_ANOMALY_BOTH;
    return AI_ANOMALY_UNKNOWN;
}

/* Column indices (resolved from header) */
typedef struct {
    int ts;       /* timestamp       */
    int mac;      /* CMMAC           */
    int cpu_mse;  /* dense_cpu_mse   */
    int mem_mse;  /* dense_mem_mse   */
    int cpu_flag; /* dense_cpu_flag  */
    int mem_flag; /* dense_mem_flag  */
    int cpu_sev;  /* dense_cpu_sev   */
    int mem_sev;  /* dense_mem_sev   */
    int atype;    /* anomaly_type    */
} ColIdx;

static int parse_header(const char *line, ColIdx *ci)
{
    char *toks[32];
    char tmp[512];
    strncpy(tmp, line, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    int n = split_csv(tmp, toks, 32);
    memset(ci, -1, sizeof(*ci));   /* initialise all to -1 */

    for (int i = 0; i < n; i++) {
        trim(toks[i]);
        if      (strcmp(toks[i], "timestamp")      == 0) ci->ts       = i;
        else if (strcmp(toks[i], "CMMAC")          == 0) ci->mac      = i;
        else if (strcmp(toks[i], "dense_cpu_mse")  == 0) ci->cpu_mse  = i;
        else if (strcmp(toks[i], "dense_mem_mse")  == 0) ci->mem_mse  = i;
        else if (strcmp(toks[i], "dense_cpu_flag") == 0) ci->cpu_flag = i;
        else if (strcmp(toks[i], "dense_mem_flag") == 0) ci->mem_flag = i;
        else if (strcmp(toks[i], "dense_cpu_sev")  == 0) ci->cpu_sev  = i;
        else if (strcmp(toks[i], "dense_mem_sev")  == 0) ci->mem_sev  = i;
        else if (strcmp(toks[i], "anomaly_type")   == 0) ci->atype    = i;
    }
    return (ci->ts >= 0 && ci->mac >= 0 && ci->atype >= 0) ? 0 : -1;
}

static float safe_f(const char *s) { return s ? (float)atof(s) : 0.f; }
static int   safe_i(const char *s) { return s && *s ? atoi(s)  : 0;   }

/* Parse one data row into an AnomalyEvent.
 * Returns 0 if an actionable (non-Normal) event was produced. */
static int parse_row(const char *line, const ColIdx *ci, AnomalyEvent *ev)
{
    char *toks[32];
    char tmp[1024];
    strncpy(tmp, line, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    int n = split_csv(tmp, toks, 32);

#define COL(idx) ((idx) >= 0 && (idx) < n ? (trim(toks[idx]), toks[idx]) : NULL)

    const char *type_str = COL(ci->atype);
    if (!type_str || strcmp(type_str, "Normal") == 0)
        return -1;   /* Normal — ignore */

    memset(ev, 0, sizeof(*ev));

    const char *ts  = COL(ci->ts);
    const char *mac = COL(ci->mac);
    if (ts)  strncpy(ev->timestamp, ts,  sizeof(ev->timestamp)  - 1);
    if (mac) strncpy(ev->mac,       mac, sizeof(ev->mac)         - 1);

    ev->anomaly_type = parse_anomaly_type(type_str);
    ev->cpu_mse      = safe_f(COL(ci->cpu_mse));
    ev->mem_mse      = safe_f(COL(ci->mem_mse));
    ev->cpu_flag     = safe_i(COL(ci->cpu_flag));
    ev->mem_flag     = safe_i(COL(ci->mem_flag));
    ev->cpu_sev      = safe_f(COL(ci->cpu_sev));
    ev->mem_sev      = safe_f(COL(ci->mem_sev));

#undef COL
    return 0;
}

/* ── Spawn a workflow thread for one event ───────────────────────────────── */

static void maybe_spawn_workflow(const AnomalyEvent *ev,
                                 const AiDiagConfig *cfg)
{
    pthread_mutex_lock(&g_mac_mutex);

    MacEntry *entry = mac_entry(ev->mac);
    if (!entry) {
        AiDiagWarn("main: MAC table full; dropping event for %s", ev->mac);
        pthread_mutex_unlock(&g_mac_mutex);
        return;
    }

    time_t now = time(NULL);

    /* Skip if already running for this MAC */
    if (entry->active) {
        AiDiagInfo("main: workflow already active for %s; skipping", ev->mac);
        pthread_mutex_unlock(&g_mac_mutex);
        return;
    }

    /* Cooldown check */
    if (entry->last_trigger > 0 &&
        difftime(now, entry->last_trigger) < cfg->cooldown_sec) {
        AiDiagInfo("main: cooldown active for %s (%.0f / %d s)",
                   ev->mac,
                   difftime(now, entry->last_trigger),
                   cfg->cooldown_sec);
        pthread_mutex_unlock(&g_mac_mutex);
        return;
    }

    /* Allocate args (WorkflowArgs) and thread context */
    WorkflowArgs *wa = malloc(sizeof(*wa));
    ThreadCtx    *ctx = malloc(sizeof(*ctx));
    if (!wa || !ctx) {
        AiDiagError("main: OOM allocating workflow args for %s", ev->mac);
        free(wa);
        free(ctx);
        pthread_mutex_unlock(&g_mac_mutex);
        return;
    }
    wa->event = *ev;
    wa->cfg   = *cfg;
    ctx->wa   = wa;
    ctx->entry = entry;

    entry->active       = 1;
    entry->last_trigger = now;
    pthread_mutex_unlock(&g_mac_mutex);

    AiDiagInfo("main: spawning workflow for %s type=%s",
               ev->mac, ai_anomaly_label(ev->anomaly_type));

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&tid, &attr, workflow_wrapper, ctx) != 0) {
        AiDiagError("main: pthread_create failed for %s", ev->mac);
        pthread_mutex_lock(&g_mac_mutex);
        entry->active = 0;
        pthread_mutex_unlock(&g_mac_mutex);
        free(wa);
        free(ctx);
    }
    pthread_attr_destroy(&attr);
}

/* ── Process newly read lines ────────────────────────────────────────────── */

static void process_lines(const char *buf, const ColIdx *ci,
                          const AiDiagConfig *cfg)
{
    /* Walk buf line by line */
    const char *p = buf;
    while (*p) {
        const char *nl = strchr(p, '\n');
        char line[1024];
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        if (len > 0) {
            AnomalyEvent ev;
            if (parse_row(line, ci, &ev) == 0) {
                AiDiagInfo("main: anomaly event: MAC=%s type=%s "
                           "CPU_SEV=%.2fx MEM_SEV=%.2fx",
                           ev.mac, ai_anomaly_label(ev.anomaly_type),
                           (double)ev.cpu_sev, (double)ev.mem_sev);
                maybe_spawn_workflow(&ev, cfg);
            }
        }
        p = nl ? nl + 1 : p + len;
        if (!nl) break;
    }
}

/* ── Daemonise ────────────────────────────────────────────────────────────── */

static void daemonize(void)
{
    switch (fork()) {
        case 0:  break;
        case -1: perror("fork"); exit(1);
        default: _exit(0);
    }
    if (setsid() < 0) { perror("setsid"); exit(1); }

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}

/* ── Usage ────────────────────────────────────────────────────────────────── */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "AI-driven diagnostic daemon for RDK-B anomaly events.\n"
        "\n"
        "Options:\n"
        "  -w <path>   CSV file to watch  (default: %s)\n"
        "  -l <path>   log file path      (default: %s)\n"
        "  -d          daemonize\n"
        "  -s          skip rows already present at startup\n"
        "  -v          verbose (log LLM request bodies)\n"
        "  -h          print this help and exit\n"
        "\n"
        "Environment variables override all defaults; see ai_diag_config.h.\n"
        "API key MUST be set via AZURE_API_KEY.\n",
        prog,
        AI_DIAG_DEFAULT_WATCH_PATH,
        AI_DIAG_LOG_PATH);
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    AiDiagConfig cfg;
    ai_diag_config_load(&cfg, NULL);   /* defaults + env */

    /* ── Command-line argument parsing ─────────────────────────────────── */
    int opt;
    while ((opt = getopt(argc, argv, "w:l:dsvh")) != -1) {
        switch (opt) {
            case 'w':
                strncpy(cfg.watch_path, optarg, sizeof(cfg.watch_path) - 1);
                break;
            case 'l':
                strncpy(cfg.log_path, optarg, sizeof(cfg.log_path) - 1);
                break;
            case 'd':
                cfg.daemonize = 1;
                break;
            case 's':
                cfg.skip_existing = 1;
                break;
            case 'v':
                cfg.verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* ── Logging ────────────────────────────────────────────────────────── */
    ai_diag_log_init(cfg.log_path);
    AiDiagInfo("AiDiag v%s starting", AI_DIAG_VERSION);

    if (!cfg.api_key[0]) {
        AiDiagError("AZURE_API_KEY is not set; exiting");
        fprintf(stderr, "Error: AZURE_API_KEY environment variable must be set.\n");
        return 1;
    }

    ai_diag_config_dump(&cfg);

    /* ── Daemonize ──────────────────────────────────────────────────────── */
    if (cfg.daemonize) {
        AiDiagInfo("daemonizing");
        daemonize();
    }

    /* ── Signal handling ────────────────────────────────────────────────── */
    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);
    signal(SIGPIPE, SIG_IGN);

    /* ── libcurl global init ────────────────────────────────────────────── */
    ai_diag_llm_global_init();

    /* ── Open the anomaly results CSV ───────────────────────────────────── */
    FILE *fp = fopen(cfg.watch_path, "r");
    if (!fp) {
        AiDiagWarn("Cannot open watch path '%s'; will retry", cfg.watch_path);
    }

    ColIdx ci;
    int    header_parsed = 0;
    long   file_offset   = 0;

    if (fp) {
        /* Detect and parse the header row */
        char line[1024];
        if (fgets(line, sizeof(line), fp)) {
            /* Heuristic: if first char is a letter it's a header row */
            if (isalpha((unsigned char)line[0])) {
                if (parse_header(line, &ci) == 0) {
                    header_parsed = 1;
                    AiDiagInfo("main: header parsed OK");
                } else {
                    AiDiagWarn("main: header parse failed; "
                               "anomaly_type column not found");
                }
            }
        }

        if (cfg.skip_existing) {
            fseek(fp, 0, SEEK_END);
            AiDiagInfo("main: skip_existing set; seeking to EOF");
        }
        file_offset = ftell(fp);
    }

#ifdef __linux__
    /* ── inotify setup ──────────────────────────────────────────────────── */
    int ifd = inotify_init1(IN_NONBLOCK);
    int iwd = -1;
    if (ifd >= 0 && fp) {
        iwd = inotify_add_watch(ifd, cfg.watch_path,
                                IN_MODIFY | IN_CLOSE_WRITE);
        if (iwd < 0)
            AiDiagWarn("main: inotify_add_watch failed: %m");
    }
#endif

    /* ── Track inode so we detect log rotation ──────────────────────────── */
    struct stat st_prev;
    memset(&st_prev, 0, sizeof(st_prev));
    if (fp) fstat(fileno(fp), &st_prev);

    AiDiagInfo("main: watching %s (poll %d ms)", cfg.watch_path,
               cfg.poll_interval_ms);

    /* ── Main event loop ────────────────────────────────────────────────── */
    while (!g_stop) {

#ifdef __linux__
        /* Wait for inotify events or timeout */
        if (ifd >= 0 && iwd >= 0) {
            struct pollfd pfd = { ifd, POLLIN, 0 };
            poll(&pfd, 1, cfg.poll_interval_ms);

            /* Drain inotify events (we just care that the file changed) */
            char evbuf[sizeof(struct inotify_event) + NAME_MAX + 1];
            while (read(ifd, evbuf, sizeof(evbuf)) > 0) {}
        } else {
            usleep((unsigned)cfg.poll_interval_ms * 1000u);
        }
#else
        usleep((unsigned)cfg.poll_interval_ms * 1000u);
#endif

        if (g_stop) break;

        /* ── Reopen CSV if it was replaced (log rotation) ─────────────── */
        struct stat st_now;
        if (stat(cfg.watch_path, &st_now) == 0) {
            if (fp && st_now.st_ino != st_prev.st_ino) {
                AiDiagInfo("main: file replaced (inode changed); reopening");
                fclose(fp);
                fp = NULL;
            }
        }

        if (!fp) {
            fp = fopen(cfg.watch_path, "r");
            if (!fp) {
                AiDiagWarn("main: cannot open '%s': %m; retrying",
                           cfg.watch_path);
                continue;
            }
            stat(cfg.watch_path, &st_prev);
            header_parsed = 0;
            file_offset   = 0;

            char line[1024];
            if (fgets(line, sizeof(line), fp) &&
                isalpha((unsigned char)line[0])) {
                if (parse_header(line, &ci) == 0)
                    header_parsed = 1;
            }
            file_offset = ftell(fp);
        }

        if (!header_parsed) {
            AiDiagDebug("main: waiting for header row in %s", cfg.watch_path);
            continue;
        }

        /* ── Read any new data since last check ─────────────────────────── */
        fseek(fp, file_offset, SEEK_SET);

        char read_buf[65536];
        size_t nread = fread(read_buf, 1, sizeof(read_buf) - 1, fp);
        if (nread > 0) {
            read_buf[nread] = '\0';
            file_offset = ftell(fp);
            process_lines(read_buf, &ci, &cfg);
        }
    }

    /* ── Cleanup ────────────────────────────────────────────────────────── */
    AiDiagInfo("main: shutting down");

    if (fp)  fclose(fp);
#ifdef __linux__
    if (ifd >= 0) close(ifd);
#endif

    ai_diag_llm_global_cleanup();
    AiDiagInfo("main: exit");
    return 0;
}
