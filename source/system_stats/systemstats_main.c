#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include "systemstats_apis.h"
#include "helper.h"

static void daemonize(void)
{
    switch (fork())
    {
        case 0:
            break;
        case -1:
            // Error
            printf("Error daemonizing (fork)! %d - %s\n", errno, strerror(errno));
            exit(0);
            break;
        default:
            _exit(0);
    }

    if (setsid() < 0)
    {
        printf("Error demonizing (setsid)! %d - %s\n", errno, strerror(errno));
        exit(0);
    }

#ifndef  _DEBUG
    int fd;
    fd = open("/dev/null", O_RDONLY);
    if (fd != 0)
    {
        dup2(fd, 0);
        close(fd);
    }
    fd = open("/dev/null", O_WRONLY);
    if (fd != 1)
    {
        dup2(fd, 1);
        close(fd);
    }
    fd = open("/dev/null", O_WRONLY);
    if (fd != 2)
    {
        dup2(fd, 2);
        close(fd);
    }
#endif
}

void sig_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM) {
        printf("Signal %d received, exiting!\n", sig);
        SystemStats_StopThread();
        SystemStats_DeInit();
        exit(0);
    }
    else if ( sig == SIGUSR1 )
    {
        signal(SIGUSR1, sig_handler); /* reset it to this function */
        printf("SIGUSR1 received!\n");
    }
    else if ( sig == SIGUSR2 )
    {
        printf("SIGUSR2 received!\n");
    }
    else if ( sig == SIGCHLD )
    {
        signal(SIGCHLD, sig_handler); /* reset it to this function */
        printf("SIGCHLD received!\n");
    }
    else if ( sig == SIGPIPE )
    {
        signal(SIGPIPE, sig_handler); /* reset it to this function */
        printf("SIGPIPE received!\n");
    }
    else
    {
        printf("Signal %d received, exiting!\n", sig);
        SystemStats_DeInit();
        exit(0);
    }
}

int main()
{
    daemonize();

    if (SystemStats_Init() != 0) {
        printf("Failed to initialize SystemStats, exiting.\n");
        return -1;
    }

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGUSR1, sig_handler);
    signal(SIGUSR2, sig_handler);
    signal(SIGSEGV, sig_handler);
    signal(SIGBUS, sig_handler);
    signal(SIGFPE, sig_handler);
    signal(SIGILL, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGHUP, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    if (SystemStats_StartThread() != 0) {
        log_message("Failed to start SystemStats thread.\n");
        SystemStats_DeInit();
        return -1;
    }

    while(1)
    {
        sleep(30);
    }

    return 0;
}

