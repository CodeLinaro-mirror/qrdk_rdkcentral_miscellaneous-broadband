/************************************************************************************
  If not stated otherwise in this file or this component's Licenses.txt file the
  following copyright and licenses apply:

  Copyright 2018 RDK Management

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
**************************************************************************/
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "secure_wrapper.h"
#ifdef UTC_ENABLE
#include <stdlib.h>
#include <unistd.h>
#include "platform_hal.h"
#include <stdbool.h>
#endif
#define DATE_MAX_STR_SIZE 26
//#define DATE_FMT "%FT%TZ%z"

#define DATE_FMT "%T"
#define DATE_FMT_H "%H"
#define DATE_FMT_M "%M"

#ifdef UTC_ENABLE

int getTimeOffsetFromSysevent(char *name)
{
    char a[100];
    FILE *fp;
    int off = -1;
    fp = v_secure_popen("r","sysevent get %s",name);
    if(fp != NULL)
    {
        fgets(a,sizeof(a),fp);
        a[strlen(a) - 1] = '\0';
        v_secure_pclose(fp);           
        if(a[0] != '\0')
        {
            if(a[0] != '@')
            {
                //Offset is Decimal now, which was converted from HEX to Decimal by DHCPMANAGER
                off = atoi(a);
            }
            else
            {
                off = atoi(a + 1);
            }
            return off;
        }
    }
    return -1;
}
#endif

time_t getOffset()
{
    time_t off = 0;

#ifdef UTC_ENABLE
    char a[100];
    char *pTimeOffset = a;

    if(access("/nvram/ETHWAN_ENABLE", 0))
    {
        if( platform_hal_getTimeOffSet(pTimeOffset) == RETURN_OK )
	{
            off = atoi(pTimeOffset);
	    return off;
	}
    }

    off = getTimeOffsetFromSysevent("ipv6-timeoffset");
    if(off != -1)
    {
        return off;
    }

    off = getTimeOffsetFromSysevent("ipv4-timeoffset");
    if(off != -1)
    {
        return off;
    }

#endif
    return off;
}
int main( int argc, char *argv[]) {

    time_t now_time, now_time_local,off;
    struct tm now_tm_local;
    char str_local[DATE_MAX_STR_SIZE];

    off = getOffset();
    
    time(&now_time);
    now_time_local =  now_time + off;
    gmtime_r(&now_time_local, &now_tm_local); // already adjusted for TZ with offset
    
     if( argc == 2 ) {
          if(argv[1][0] == 'H')
          {
              strftime(str_local, DATE_MAX_STR_SIZE, DATE_FMT_H, &now_tm_local);
              printf("%s\n", str_local);
          }
     if(argv[1][0] == 'M')
      {
          strftime(str_local, DATE_MAX_STR_SIZE, DATE_FMT_M, &now_tm_local);
          printf("%s\n", str_local);
      }
       if (argv[1][0] == 'O') // print offset
      {
            printf("%ld\n", off);
      }
  }
  else if( argc > 2 ) {
     printf("Too many arguments supplied.\n");
  }
  else {
    strftime(str_local, DATE_MAX_STR_SIZE, DATE_FMT, &now_tm_local);
    printf("%s\n", str_local);
  }

   return 0;
}
