/*
 * Copyright 2025 RDK Management — Apache-2.0
 *
 * ai_diag_cmd.c — secure diagnostic command execution
 */

#define _GNU_SOURCE 1

#include "ai_diag_cmd.h"
#include "ai_diag_log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ── Security policy tables ─────────────────────────────────────────────── */

/*
 * Allow-list: the trimmed command string must START with one of these prefixes.
 * Only read-only, diagnostic utilities are permitted.
 * Entries ending with a space require at least one argument; entries without
 * a trailing space match the command name exactly (e.g. "uptime").
 */
static const char *const g_allow_prefixes[] = {
    /* System stats */
    "top",
    "ps",
    "uptime",
    "free",
    "vmstat",
    "df ",
    "iostat",
    "mpstat",
    "sar ",

    /* File/proc reading */
    "cat /proc/",
    "cat /rdklogs/",
    "cat /sys/",
    "cat /tmp/",
    "cat /etc/",
    "cat /var/",
    "ls /proc/",
    "ls -l /proc/",
    "ls -la /proc/",
    "ls /rdklogs/",
    "ls /tmp/",
    "ls -l /tmp/",
    "ls -la /tmp/",
    "ls /var/",
    "ls -la /var/",
    "du ",

    /* Kernel / system log */
    "dmesg",
    "logread",
    "journalctl",

    /* Network state (read-only) */
    "ifconfig",
    "ip addr",
    "ip link",
    "ip route",
    "ip neigh",
    "netstat",
    "ss ",

    /* RDK-B platform utilities (read-only) */
    "dmcli eRT getv ",
    "dmcli eRT getv",
    "wifi_api wifi_get",
    "wifi_api wifi_getAp",
    "wifi_api wifi_getRadio",
    "sysevent get ",

    /* Process introspection */
    "pidof ",
    "pgrep ",

    /* General text utilities (as pipeline stages) */
    "date",
    "hostname",
    "uname",

    NULL   /* sentinel */
};

/*
 * Block-list: if ANY of these substrings appear anywhere in the command,
 * execution is refused.  Checked AFTER the allow-list to catch dangerous
 * pipeline stages appended to an otherwise-safe leading command.
 */
static const char *const g_block_substrings[] = {
    /* Destructive file operations */
    "rm ",  "rm\t",  "rmdir",
    "dd if=", "dd of=", "mkfs", "fdisk", "parted",
    "mv ",  "cp ",

    /* Privilege / configuration changes */
    "chmod", "chown", "chattr",

    /* Network exfiltration */
    "wget ", "wget\t",
    "curl ", "curl\t",
    "nc ",   "ncat ", "netcat",
    "scp ",  "sftp ", "rsync",

    /* Destructive system commands */
    "shutdown", "reboot", "poweroff", "halt", "init 0", "init 6",
    "kill -9",  "killall ", "pkill ",

    /* Shell code-execution escalation */
    "eval ", "eval\t",
    "exec ", "exec\t",
    "source ", ". /",

    /* Firewall / network rule changes */
    "iptables ", "ip6tables ",

    /* Write redirects (prevent writing to files) */
    "> /", ">> /", "tee /",

    /* Package management */
    "apt ", "apt-get", "opkg ", "yum ", "dnf ",

    NULL   /* sentinel */
};

/* ── ai_diag_cmd_is_allowed ──────────────────────────────────────────────── */

int ai_diag_cmd_is_allowed(const char *cmd)
{
    if (!cmd || !*cmd) {
        AiDiagWarn("cmd_validate: empty command rejected");
        return 0;
    }

    /* 1. Skip leading whitespace for a clean comparison */
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;

    /* 2. Block-list check: scan the entire command string */
    for (int i = 0; g_block_substrings[i]; i++) {
        if (strstr(cmd, g_block_substrings[i])) {
            AiDiagWarn("cmd_validate: blocked pattern '%s' in: %.160s",
                       g_block_substrings[i], cmd);
            return 0;
        }
    }

    /* 3. Allow-list check: the command must start with an allowed prefix */
    for (int i = 0; g_allow_prefixes[i]; i++) {
        const char *pfx   = g_allow_prefixes[i];
        size_t      pfxlen = strlen(pfx);
        if (strncmp(cmd, pfx, pfxlen) == 0) {
            /* If the prefix ends with a space or a '/' it is a true prefix:
             *   space — the command takes mandatory arguments after it.
             *   '/'   — the prefix is a path root; any sub-path is allowed.
             * Otherwise (bare command name) the next char must be space,
             * pipe, null, or end-of-string to avoid matching a longer name. */
            char last = pfx[pfxlen - 1];
            if (last == ' ' || last == '/')
                return 1;
            char next = cmd[pfxlen];
            if (next == '\0' || next == ' ' || next == '\t' || next == '|')
                return 1;
        }
    }

    AiDiagWarn("cmd_validate: no allow-list match for: %.160s", cmd);
    return 0;
}

/* ── ai_diag_cmd_run ─────────────────────────────────────────────────────── */

char *ai_diag_cmd_run(const char *cmd, int timeout_sec)
{
    /* Validate before any execution */
    if (!ai_diag_cmd_is_allowed(cmd)) {
        /* Return a safe placeholder; caller always gets a non-NULL string */
        const char *msg = "[BLOCKED: command not permitted by security policy]";
        char *r = malloc(strlen(msg) + 1);
        if (r) strcpy(r, msg);
        else   r = strdup("OOM");
        return r;
    }

    AiDiagDebug("cmd_run: executing: %.200s", cmd);

    /* Redirect stderr to stdout so both streams appear in the output */
    char cmd_merged[AI_DIAG_MAX_CMD_LEN + 8];
    snprintf(cmd_merged, sizeof(cmd_merged), "%s 2>&1", cmd);

    FILE *fp = popen(cmd_merged, "r");
    if (!fp) {
        AiDiagError("cmd_run: popen failed for '%s': %s", cmd, strerror(errno));
        char *r = strdup("[ERROR: popen failed]");
        return r ? r : strdup("OOM");
    }

    int fd = fileno(fp);

    /* Allocate output buffer (one extra byte for null + one for TRUNCATED tag) */
    char  *out  = malloc(AI_DIAG_MAX_CMD_OUTPUT + 64);
    if (!out) {
        pclose(fp);
        return strdup("[ERROR: out-of-memory]");
    }

    size_t total = 0;
    int    timed_out = 0;

    struct timeval deadline;
    gettimeofday(&deadline, NULL);
    deadline.tv_sec += timeout_sec;

    while (total < AI_DIAG_MAX_CMD_OUTPUT) {
        struct timeval now, remaining;
        gettimeofday(&now, NULL);

        /* Compute remaining time */
        remaining.tv_sec  = deadline.tv_sec  - now.tv_sec;
        remaining.tv_usec = deadline.tv_usec - now.tv_usec;
        if (remaining.tv_usec < 0) {
            remaining.tv_sec--;
            remaining.tv_usec += 1000000;
        }
        if (remaining.tv_sec < 0 ||
            (remaining.tv_sec == 0 && remaining.tv_usec <= 0)) {
            timed_out = 1;
            break;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        int sel = select(fd + 1, &rfds, NULL, NULL, &remaining);
        if (sel < 0) {
            if (errno == EINTR)
                continue;   /* interrupted by signal; retry */
            break;          /* unexpected error */
        }
        if (sel == 0) {
            timed_out = 1;
            break;
        }

        ssize_t nr = read(fd, out + total, AI_DIAG_MAX_CMD_OUTPUT - total);
        if (nr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (nr == 0)
            break;   /* EOF */
        total += (size_t)nr;
    }
    out[total] = '\0';

    pclose(fp);   /* waits for child; will reap even on timeout */

    if (timed_out) {
        AiDiagWarn("cmd_run: timeout (%ds) for: %.160s", timeout_sec, cmd);
        strncat(out, "\n[TIMEOUT]", 63);
    } else if (total >= AI_DIAG_MAX_CMD_OUTPUT) {
        AiDiagDebug("cmd_run: output truncated for: %.80s", cmd);
        strncat(out, "\n[TRUNCATED]", 63);
    }

    AiDiagDebug("cmd_run: captured %zu bytes for: %.80s", total, cmd);
    return out;
}

/* ── ai_diag_cmd_run_batch ───────────────────────────────────────────────── */

void ai_diag_cmd_run_batch(const char * const *cmds,
                           char              **outputs,
                           int                 n,
                           int                 timeout_sec)
{
    for (int i = 0; i < n; i++) {
        AiDiagInfo("cmd_batch [%d/%d]: %s", i + 1, n,
                   cmds[i] ? cmds[i] : "(null)");
        outputs[i] = ai_diag_cmd_run(cmds[i] ? cmds[i] : "", timeout_sec);
    }
}

/* ── ai_diag_execute_actions ─────────────────────────────────────────────── */

/*
 * Validate a corrective action string.
 * Only "systemctl restart <service>" is permitted.
 * Service names must contain only [A-Za-z0-9._-].
 */
static int action_is_allowed(const char *action)
{
    static const char prefix[] = "systemctl restart ";
    const size_t pfxlen = sizeof(prefix) - 1;

    if (!action || strncmp(action, prefix, pfxlen) != 0)
        return 0;

    const char *svc = action + pfxlen;
    if (*svc == '\0')
        return 0;

    for (const char *p = svc; *p; p++) {
        if (!isalnum((unsigned char)*p) &&
            *p != '-' && *p != '_' && *p != '.') {
            return 0;
        }
    }
    return 1;
}

void ai_diag_execute_actions(const LlmResponse *resp)
{
    if (!resp || resp->num_actions == 0)
        return;

    AiDiagInfo("action_exec: evaluating %d recommended action(s)",
               resp->num_actions);

    for (int i = 0; i < resp->num_actions; i++) {
        const char *action = resp->recommended_action[i];

        if (!action_is_allowed(action)) {
            AiDiagInfo("action_exec [%d/%d]: skipping (not permitted): %.160s",
                       i + 1, resp->num_actions, action ? action : "(null)");
            continue;
        }

        AiDiagInfo("action_exec [%d/%d]: executing: %s",
                   i + 1, resp->num_actions, action);

        char cmd_merged[AI_DIAG_MAX_CMD_LEN + 8];
        snprintf(cmd_merged, sizeof(cmd_merged), "%s 2>&1", action);

        FILE *fp = popen(cmd_merged, "r");
        if (!fp) {
            AiDiagError("action_exec: popen failed for '%s': %s",
                        action, strerror(errno));
            continue;
        }

        char buf[512] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        pclose(fp);

        AiDiagInfo("action_exec [%d/%d]: result: %s",
                   i + 1, resp->num_actions,
                   buf[0] ? buf : "(no output)");
    }
}
