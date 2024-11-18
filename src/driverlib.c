/*********************************************************************** 
 *
 * driverlib.c - A package of helper functions for C Autolab drivers
 *
 * Copyright (c) 2004, D. O'Hallaron, All rights reserved.  May not be
 * used, modified, or copied without permission.
 *
# $Id: driverlib.c,v 1.6 2006/11/21 03:44:23 autolab Exp $
 *********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>

#include "driverhdrs.h"
#include "driverlib.h"

/**************************
 * Private helper functions 
 **************************/

/*
 * sigalrm_handler - handles SIGALRM timeout signals
 */

void sigalrm_handler(int sig) {
    fprintf(stderr, "Program timed out after %d seconds\n", AUTOGRADE_TIMEOUT);
    exit(1);
}

/******************
 * Public functions
 ******************/

/*
 * init_timeout - Time out the driver if student code hangs.
 * The argument is in seconds; -1 means to use the AUTOGRADE_TIMEOUT, 0
 * means never timeout.
 */
void init_timeout(int timeout) {
    if (timeout == 0) {
	return;
    }
    if (timeout < 0) {
	timeout = AUTOGRADE_TIMEOUT;
    }
    signal(SIGALRM, sigalrm_handler);
    alarm(timeout); 
}

/* 
 * init_driver - Initialize the driverlib package
 */
int init_driver(char *status_msg) 
{
    /* Ignore any terminating SIGPIPE signals */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGPOLL, SIG_IGN);
    signal(SIGPOLL, SIG_IGN);

    return 0;
}    

/*
 * driver_post - This is the routine that the driver calls when
 *     it needs to transmit an autoresult string to Autolab
 */
int driver_post(char *userid, char *result, int autograded, char *status_msg) 
{
    return 0;
}
