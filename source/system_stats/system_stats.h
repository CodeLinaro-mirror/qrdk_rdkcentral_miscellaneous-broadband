#ifndef SYSTEM_STATS_H
#define SYSTEM_STATS_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "helper.h"
#include "secure_wrapper.h"

// Structure to store system statistics
typedef struct {
    char cmac[32];
    double cpu_usage;
    double used_memory_kb;
    double loadavg_15min;
    double avail_memory_kb;
    double free_memory_kb;
    double slab_memory_kb;
    int client_count_2g;
    int client_count_5g;
    int client_count_6g;
} SystemStats;

// Function declarations
void initialize_system_stats(SystemStats *stats);
void collect_system_stats(SystemStats *stats);
void get_cpu_usage(double *cpu_usage);
void get_memory_info(double *used_memory_kb, double *free_memory_kb, double *avail_memory_kb, double *slab_memory_kb);
void get_load_average(double *load15);
void get_cmac_address(char *cmac, size_t size);
void get_wifi_client_counts(int *count_2g, int *count_5g, int *count_6g);

#endif // SYSTEM_STATS_H
