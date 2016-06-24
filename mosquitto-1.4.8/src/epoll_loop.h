/*
Copyright (c) 2016 
All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.
 
The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.
 
Contributors:
   liufan - initial implementation and documentation.
*/


#ifndef _EPOLL_H_
#define _EPOLL_H_

#include <config.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#include "mosquitto_internal.h"
#include "mosquitto_broker.h"
#include "send_mosq.h"
#include "time_mosq.h"
#include "util_mosq.h"
#include "memory_mosq.h"

#define DEFAULT_EPOLL_FD_SIZE 1024

int is_listensock(int * listensock,int listensock_count,int eventfd);

int mosquitto_main_epoll_loop(struct mosquitto_db *db, int *listensock, int listensock_count, int listener_max);

static void epoll_handle_event(struct mosquitto_db *db,struct epoll_event *events,int event_num,int *listensock, int listensock_count);
static int epoll_handle_read_event(struct mosquitto_db *db,struct epoll_event *events);
static int epoll_handle_write_event(struct mosquitto_db *db,struct epoll_event *events);
static int epoll_handle_error_event(struct mosquitto_db *db,struct epoll_event *events);

void epoll_do_disconnect(struct mosquitto_db *db, struct mosquitto *context);

int create_epoll(struct mosquitto_db *db);
void destory_epoll(struct mosquitto_db *db);

int invalid_event(int state);
int register_event(struct mosquitto_db * db,int fd,int state);
int unregister_event(struct mosquitto_db * db,int fd);
int modify_event(struct mosquitto_db * db,int fd,int state);

#endif


