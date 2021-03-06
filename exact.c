/* $Id: exact.c,v 1.19 2004/03/31 20:25:21 doug Exp $
 * 
 * This file is part of EXACT.
 *
 * EXACT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * EXACT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with EXACT; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <regex.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <string.h>

#ifdef HAVE_GETOPT_H
    #include <getopt.h>
#else
    #include "getopt.h"
#endif

#include "tail.h"
#include "logger.h"
#include "match.h"
#include "conffile.h"
#include "auth.h"
#include "daemon.h"
#include "errno.h"

typedef struct {
    int foreground;
    int sleep;
    int debug;
    int preserve;
    int tables;
} command_line;

command_line cmd;

int onepass() {
    int i;
    int start=0;
    match_login *l;

    tail_read();
    for(i=0;i<tail_bufflen;i++) {
        if(tail_buff[i]=='\n' || tail_buff[i]=='\0') {
            tail_buff[i]='\0';
            l=match_line(tail_buff+start);
            if(l) auth_add(l->username, l->hostname);
            start=i+1;
        }
    }
    return 0;
}

void usage() {
    fprintf(stderr,"Usage: exact [-h] [-d] [-f] [-p] [-c filename]\n");
    fprintf(stderr,"       -h | --help         show this usage information\n");
    fprintf(stderr,"       -d | --debug        more than you ever want to know\n");
    fprintf(stderr,"       -f | --foreground   don't background\n");
    fprintf(stderr,"       -p | --preserve     don't remove the relay file on exit\n");
    fprintf(stderr,"       -c | --config       configuration filename\n");
    fprintf(stderr,"       -V | --version      print the version of exact and exit\n");
    fprintf(stderr,"                           (default %s)\n", conffile_name());
    fprintf(stderr,"see the manual page exact(8) for more information\n");
}

void version() {
    fprintf(stderr, "Exact Version 1.40 (c) 2004, Doug Winter\n");
}

void cmdline(int argc, char *argv[]) {
    int c;

    cmd.foreground=0;
    cmd.sleep=0;
    cmd.debug=0;
    cmd.preserve=0;
    while(1) {
        int option_index=0;
        static struct option long_options[] = {
            {"help", 0, NULL, 'h'},
            {"foreground",  0,  NULL, 'f'},
            {"sleep", 0, NULL, 's'},
            {"debug", 0, NULL, 'd'},
            {"preserver", 0, NULL, 'p'},
            {"config", 1, NULL, 'c'},
            {"version", 0, NULL, 'V'},
            {0,0,0,0}
        };
        c=getopt_long(argc,argv,"phsfdc:V",long_options, &option_index);
        if(c==-1)
            break;
        switch(c) {
            case 'h':
                usage();
                exit(0);
            case 'V':
                version();
                exit(0);
            case 'f':
                cmd.foreground=1;
                break;
            case 's':
                cmd.sleep=1;
                break;
            case 'd':
                cmd.debug=1;
                break;
            case 'p':
                cmd.preserve=1;
                break;
            case 'c':
                conffile_setname(optarg);
                break;
            default:
                fprintf(stderr, "Unknown argument: %c\n",c);
                usage();
                exit(40);
                break;
        }
    }
}

void checkpid() {
    FILE *f;
    f=fopen(conffile_param("pidfile"),"r");
    if(f) {
        int opid=0;
        fscanf(f,"%d",&opid);
        fclose(f);
        if(opid) {
            int kr=kill(opid,0);
            if(kr   != -1) {
                logger(LOG_ERR, "Exact is already running, with pid %d\n", opid);
                exit(41);
            }
        }
    }
    logger(LOG_DEBUG, "exact is not already running\n");
}

void writepid() {
    FILE *f=fopen(conffile_param("pidfile"),"w");
    if(!f) {
        logger(LOG_ERR, "Cannot write to pid file %s\n", conffile_param("pidfile"));
        exit(42);
    }
    chmod(conffile_param("pidfile"),0640);
    logger(LOG_DEBUG, "Writing pid to %s\n", conffile_param("pidfile"));
    fprintf(f,"%d",(int)getpid());
    fclose(f);
}

void exit_handler(int s) {
    unlink(conffile_param("pidfile"));
    auth_exit();
    if(!cmd.preserve)
        unlink(conffile_param("authfile"));
    logger(LOG_ERR, "terminated\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    int use_syslog=0;
    cmdline(argc,argv);
    logger_init(0,cmd.debug,NULL);
    conffile_read();
    conffile_check();
    checkpid();
    match_init();
    daemonize(cmd.foreground, cmd.sleep);
    auth_init();
    if(!strcmp("syslog",conffile_param("logging")))
        use_syslog=1;
    else {
        if(!strcmp("internal",conffile_param("logging")))
            use_syslog=0;
        else {
            logger(LOG_ERR, "logging parameter is neither syslog nor internal\n");
            exit(100);
        }
    }
    if(cmd.foreground) {
        logger_init(0,cmd.debug,NULL); // use stderr
    } else {
        // never debug using syslog, because you might
        // get a loop
        logger_init(use_syslog,!use_syslog && cmd.debug,conffile_param("logfile")); 
        logger(LOG_DEBUG, "Daemonized\n");
    }
    writepid();
    signal(1,conffile_reload);
    signal(10,auth_dump);
    signal(15,exit_handler);
    if(!tail_open()) {
        logger(LOG_ERR,"open of %s failed.  Quitting.\n", conffile_param("maillog"));
        return 2;
    }
    logger(LOG_NOTICE, "running\n");
    while(1) {
        onepass();
    }
    assert(0);
    return 0;
}

