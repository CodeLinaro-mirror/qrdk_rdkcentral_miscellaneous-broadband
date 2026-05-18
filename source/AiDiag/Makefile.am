##########################################################################
# If not stated otherwise in this file or this component's LICENSE file
# the following copyright and licenses apply:
#
# Copyright 2025 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################

# ── AiDiag — AI-driven diagnostic daemon ─────────────────────────────────────
#
# Binary: AiDiag
#
# Dependencies:
#   libcurl    — HTTP transport for the Anthropic Messages API
#   libpthread — per-anomaly-event workflow threads + log mutex
#
# Optional (for RDK-B platform builds):
#   -lrdkloggers   add to AI_Diag_LDFLAGS if RDK_LOG integration is desired
#   -lsecure_wrapper if v_secure_popen is preferred over standard popen
##########################################################################

bin_PROGRAMS = AiDiag

AiDiag_SOURCES = \
    ai_diag_log.c      \
    ai_diag_config.c   \
    ai_diag_json.c     \
    ai_diag_cmd.c      \
    ai_diag_llm.c      \
    ai_diag_workflow.c \
    ai_diag_main.c

# libcurl header discovery via pkg-config; fall back to plain -I/usr/include
AiDiag_CFLAGS = $(AM_CFLAGS) @CURL_CFLAGS@

# -lpthread: workflow threads and log mutex
# -lcurl:    HTTP transport
AiDiag_LDFLAGS = @CURL_LIBS@ -lpthread
