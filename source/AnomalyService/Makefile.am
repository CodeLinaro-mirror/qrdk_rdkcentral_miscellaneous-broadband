##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2026 RDK Management
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

bin_PROGRAMS = AnomalyService

AnomalyService_SOURCES = \
    anomaly_service_log.c     \
    anomaly_service_csv.c     \
    anomaly_service_rbus.c    \
    anomaly_service_main.c

AnomalyService_CFLAGS = \
    -I${PKG_CONFIG_SYSROOT_DIR}$(includedir)/rbus       \
    -DANOMALY_APP_BINARY=\"/usr/bin/anomaly_app\"        \
    -DANOMALY_CPU_CONFIG=\"/usr/share/tflite_model/cpu_anomaly_config.json\"    \
    -DANOMALY_MEM_CONFIG=\"/usr/share/tflite_model/memory_anomaly_config.json\" \
    -DANOMALY_TELEMETRY_CSV=\"/rdklogs/logs/system_stats_data.csv\"  \
    -DANOMALY_RESULT_CSV=\"/rdklogs/logs/anomaly_results.csv\"       \
    -DANOMALY_DELEGATE_PATH=\"/usr/lib/libbstorm_external_delegate.so\" \
    -DANOMALY_DELEGATE_OPTIONS=\"bstm:1\;bstm-client-mode:0\;dynamic-tensors:1\"

AnomalyService_LDFLAGS = \
    -lrbus      \
    -lpthread   \
    -lrdkloggers
