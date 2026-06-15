#ifndef SYSTEMSTATS_APIS_H
#define SYSTEMSTATS_APIS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "ansc_platform.h"
#include "system_stats.h"
#include "helper.h"

//#define DEFAULT_INTERVAL 900 // 15 minutes in seconds
//#define DEFAULT_INTERVAL 300 // 5 minutes in seconds
#define DEFAULT_INTERVAL 120 // 2 minutes in seconds


// Function declarations
int SystemStats_Init();
int SystemStats_Collect();
int SystemStats_Save();
int SystemStats_DeInit();
int SystemStats_StartThread();
void SystemStats_StopThread();

#endif // SYSTEMSTATS_APIS_H
