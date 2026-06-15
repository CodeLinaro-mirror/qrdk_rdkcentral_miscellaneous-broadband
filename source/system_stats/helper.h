#ifndef HELPER_H
#define HELPER_H

#include <stddef.h>

#define LOG_FILE "/rdklogs/logs/system_stats_logs.txt"

// Function declarations
void log_message(const char *format, ...);
void execute_command(const char *command, char *output, size_t size);
void get_current_timestamp(char *timestamp, size_t size);

#endif // HELPER_H
