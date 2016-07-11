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

#include "epoll_loop.h"

extern bool flag_reload;
#ifdef WITH_PERSISTENCE
extern bool flag_db_backup;
#endif
extern bool flag_tree_print;
extern int run;
#ifdef WITH_SYS_TREE
extern int g_clients_expired;
#endif

unsigned long g_epoll_register_fd = 0;
unsigned long g_epoll_unregister_fd = 0;
unsigned long g_epoll_monitor_fd = 0;//not use
unsigned long g_epoll_expired_fd = 0;
unsigned long g_epoll_sock_connections = 0;
unsigned long g_epoll_mqtt_connections = 0;
unsigned long g_epoll_publish_num = 0;
unsigned long g_epoll_subscribe_num = 0;
unsigned long g_epoll_unsubscribe_num = 0;

int create_epoll(struct mosquitto_db * db)
{
    db->epoll_fd = epoll_create(DEFAULT_EPOLL_FD_SIZE);
    if (db->epoll_fd == -1)
    {
        switch(errno){
    		case EINVAL:
            case ENFILE:
    			return MOSQ_ERR_INVAL;
            case ENOMEM:
                return MOSQ_ERR_NOMEM;
    		default:
    			return MOSQ_ERR_ERRNO;
    	}
    }
    return MOSQ_ERR_SUCCESS;
}

void destory_epoll(struct mosquitto_db * db)
{
    assert(db);
    if(db->epoll_fd > 0)
    {
        close(db->epoll_fd);
    }
}

int invalid_event(unsigned int state)
{
    if(state > EPOLLET || state < EPOLLIN)
        return -1;
    return MOSQ_ERR_SUCCESS;
}

int register_event(struct mosquitto_db * db,int fd,int state)
{
    if(!db || db->epoll_fd <= 0 || fd <=0)
        return -1;
    if(invalid_event(state) < 0)
    {
        return -1;
    }
    struct epoll_event _new_events;
    _new_events.data.fd = fd;
    _new_events.events = state;
    if(epoll_ctl(db->epoll_fd,EPOLL_CTL_ADD,_new_events.data.fd,&_new_events) < 0){
        _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE,"[fd=%d] epoll register_event fail",_new_events.data.fd);
        return -1;
    }
    g_epoll_register_fd++;
    return MOSQ_ERR_SUCCESS;
}

int unregister_event(struct mosquitto_db * db,int fd)
{
    if(!db || db->epoll_fd <= 0 || fd <=0)
        return -1;
    if(epoll_ctl(db->epoll_fd,EPOLL_CTL_DEL,fd,NULL) < 0){
        _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "[fd=%d] epoll unregister_event fail",fd);
        return -1;
    }
    g_epoll_unregister_fd++;
    return MOSQ_ERR_SUCCESS;
}


int modify_event(struct mosquitto_db * db,int fd,int state)
{
    if(!db || db->epoll_fd <= 0 || fd <=0)
        return -1;
    if(invalid_event(state) < 0)
    {
        return -1;
    }
    struct epoll_event _new_events;
    _new_events.data.fd = fd;
    _new_events.events = state;
    if(epoll_ctl(db->epoll_fd,EPOLL_CTL_MOD,_new_events.data.fd,&_new_events) < 0){
        _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "[fd=%d] epoll modify_event fail",_new_events.data.fd);
        return -1;
    }       
    return MOSQ_ERR_SUCCESS;
}

int is_listensock(int * listensock,int listensock_count,int eventfd)
{
    int i;
    for(i = 0;i < listensock_count;++i)
    {
        if(eventfd == listensock[i])
            return 1;
    } 
    return 0;
}

void epoll_do_disconnect(struct mosquitto_db * db,struct mosquitto * context)
{
    unregister_event(db,context->sock);
    do_disconnect(db,context);
}

int epoll_handle_read_event(struct mosquitto_db * db,struct epoll_event * events)
{
    assert(db);
    assert(events);
    int fd = 0;
    struct mosquitto *context = NULL;
    fd = events->data.fd;
    HASH_FIND(hh_sock,db->contexts_by_sock,&fd,sizeof(fd),context);
    if(context == NULL)
    {
        return MOSQ_ERR_NOT_FOUND;
    }
    
#ifdef WITH_TLS
	if(events->events & EPOLLIN ||
				(context->ssl && context->state == mosq_cs_new)){
#else
		if(events->events & EPOLLIN){
#endif
    		do{
    			if(_mosquitto_packet_read(db, context)){
    				epoll_do_disconnect(db, context);
    			}
    		}while(SSL_DATA_PENDING(context));
    }
    return MOSQ_ERR_SUCCESS;
}

int epoll_handle_write_event(struct mosquitto_db * db,struct epoll_event * events)
{
    assert(db);
    assert(events);
    int fd = 0;
    int err = 0;
    socklen_t len;
    struct mosquitto *context = NULL;
    fd = events->data.fd;
    HASH_FIND(hh_sock,db->contexts_by_sock,&fd,sizeof(fd),context);
    if(context == NULL)
    {
        return MOSQ_ERR_NOT_FOUND;
    }
#ifdef WITH_TLS
	if(events->events & EPOLLOUT ||
				context->want_write ||
				(context->ssl && context->state == mosq_cs_new)){
#else
		if(events->events & EPOLLOUT){
#endif
			if(context->state == mosq_cs_connect_pending){
				len = sizeof(int);
				if(!getsockopt(context->sock, SOL_SOCKET, SO_ERROR, (char *)&err, &len)){
					if(err == 0){
						context->state = mosq_cs_new;
					}
				}else{
					epoll_do_disconnect(db, context);
				}
			}
			if(_mosquitto_packet_write(context)){
				epoll_do_disconnect(db, context);
			}
            modify_event(db,fd,EPOLLIN);
		}
    return MOSQ_ERR_SUCCESS;
    
}

int epoll_handle_error_event(struct mosquitto_db * db,struct epoll_event * events)
{

    assert(db);
    assert(events);
    int fd = 0;
    struct mosquitto *context = NULL;
    fd = events->data.fd;
    HASH_FIND(hh_sock,db->contexts_by_sock,&fd,sizeof(fd),context);
    if(context == NULL)
    {
        return -1;
    }
    epoll_do_disconnect(db,context);
    return MOSQ_ERR_SUCCESS;
}

void epoll_handle_event(struct mosquitto_db * db,struct epoll_event * events,int event_num,int * listensock,int listensock_count)
{
    assert(db);
    assert(events);
    int i;
    int new_sock = 0;
    for(i = 0;i < event_num; ++i)
    {
         if(events[i].events & EPOLLERR || events[i].events & EPOLLHUP /*|| (!(events[i].events & (EPOLLIN | EPOLLPRI | EPOLLOUT)))*/)
         {
              _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "[fd=%d] event error!",events[i].data.fd);
              if(epoll_handle_error_event(db,events+i) == -1)
              {
                  _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "epoll_handle_error_event[fd=%d] fail",events[i].data.fd);
              }
              continue;
         }
        
         if(is_listensock(listensock,listensock_count,events[i].data.fd) && events[i].events & (EPOLLIN | EPOLLPRI))
         {
             while((new_sock = mqtt3_socket_accept(db, events[i].data.fd)) == -1){
                 _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "epoll mqtt3_socket_accept[listenfd=%d] fail",events[i].data.fd);
			 }
             register_event(db,new_sock,EPOLLIN);
             continue;
         }
         if(events[i].events & EPOLLOUT)
         {
              if(epoll_handle_write_event(db,events+i) == -1)
              {
                  _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "epoll_handle_write_event[fd=%d] fail",events[i].data.fd);
              }
         }
         if(events[i].events & EPOLLIN)
         {
              if(epoll_handle_read_event(db,events+i) == -1)
              {
                  _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "epoll_handle_read_event[fd=%d] fail",events[i].data.fd);
              }
         }
    }
}


int mosquitto_main_epoll_loop(struct mosquitto_db *db, int *listensock, int listensock_count, int listener_max)
{
#ifdef WITH_SYS_TREE
        time_t start_time = mosquitto_time();
#endif
#ifdef WITH_PERSISTENCE
        time_t last_backup = mosquitto_time();
#endif
        time_t now = 0;
        time_t now_time;
        int time_count;
        int fdcount;
        struct mosquitto *context, *ctxt_tmp;
#ifndef WIN32
        sigset_t sigblock, origsig;
#endif
        int i;
#ifdef WITH_BRIDGE
        mosq_sock_t bridge_sock;
        int rc;
#endif
        int context_count = 0;
        time_t expiration_check_time = 0;
        time_t last_timeout_check = 0;
        char *id;
        
        //epoll新增加变量
        int sock_num = 0;
        struct epoll_event* epoll_events = NULL;
    
#ifndef WIN32
        sigemptyset(&sigblock);
        sigaddset(&sigblock, SIGINT);
#endif
    
        if(db->config->persistent_client_expiration > 0){
            expiration_check_time = time(NULL) + 3600;
        }
        //创建epoll句柄
        if (MOSQ_ERR_SUCCESS != create_epoll(db))
        {
            _mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "[mosquitto_main_epoll_loop] epoll_create fail.");
            return MOSQ_ERR_ERRNO;
        }
        
        //增加监听sock到epoll event
        for(i=0; i<listensock_count; i++){
            if(MOSQ_ERR_SUCCESS != register_event(db, listensock[i],EPOLLIN)){
                _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "[epoll_mosquitto_main_loop] : epoll_ctl fail (listen socket fd = %d)",listensock[i]);
                return MOSQ_ERR_UNKNOWN;;
            }
        }
        
#ifdef WITH_BRIDGE
        for(i=0; i<db->bridge_count; i++){
            if(MOSQ_ERR_SUCCESS != register_event(db,db->bridges[i]->sock,EPOLLIN)){
                _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "[epoll_mosquitto_main_loop] : bridge epoll_ctl fail (bridge socket fd = %d)",db->bridges[i]->sock);
                return MOSQ_ERR_UNKNOWN;;
            }
        }
        context_count += db->bridge_count;
#endif

        if(listensock_count + context_count > sock_num){
            sock_num = listensock_count + DEFAULT_EPOLL_FD_SIZE;
            epoll_events = _mosquitto_realloc(epoll_events,sizeof(struct epoll_event) * sock_num);
            if(!epoll_events){
                _mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "[epoll_mosquitto_main_loop] Error: Out of memory.");
                return MOSQ_ERR_NOMEM;
            }
        }
        
        while(run){
            
            mosquitto__free_disused_contexts(db);
#ifdef WITH_SYS_TREE
            if(db->config->sys_interval > 0){
                mqtt3_db_sys_update(db, db->config->sys_interval, start_time);
            }
#endif
            now_time = time(NULL);
    
            time_count = 0;
            HASH_ITER(hh_sock, db->contexts_by_sock, context, ctxt_tmp){
                if(time_count > 0){
                    time_count--;
                }else{
                    time_count = 1000;
                    now = mosquitto_time();
                }
                context->pollfd_index = -1;
    
                if(context->sock != INVALID_SOCKET){
#ifdef WITH_BRIDGE
                    if(context->bridge){
                        _mosquitto_check_keepalive(db, context);
                        if(context->bridge->round_robin == false
                                && context->bridge->cur_address != 0
                                && now > context->bridge->primary_retry){
    
                            if(_mosquitto_try_connect(context, context->bridge->addresses[0].address, context->bridge->addresses[0].port, &bridge_sock, NULL, false) <= 0){
                                COMPAT_CLOSE(bridge_sock);
                                _mosquitto_socket_close(db, context);
                                context->bridge->cur_address = context->bridge->address_count-1;
                            }
                        }
                    }
#endif
    
                    /* Local bridges never time out in this fashion. */
                    if(!(context->keepalive)
                            || context->bridge
                            || now - context->last_msg_in < (time_t)(context->keepalive)*3/2){
    
                        if(mqtt3_db_message_write(db, context) == MOSQ_ERR_SUCCESS){
                            if(context->current_out_packet || context->state == mosq_cs_connect_pending){
                                modify_event(db,context->sock,EPOLLOUT | EPOLLIN);
                            }
                        }else{
                            epoll_do_disconnect(db, context);
                        }
                    }else{
                        if(db->config->connection_messages == true){
                            if(context->id){
                                id = context->id;
                            }else{
                                id = "<unknown>";
                            }
                            _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Client %s has exceeded timeout, disconnecting.", id);
                        }
                        /* Client has exceeded keepalive*1.5 */
                        epoll_do_disconnect(db, context);
                    }
                }
            }
    
#ifdef WITH_BRIDGE
            time_count = 0;
            for(i=0; i<db->bridge_count; i++){
                if(!db->bridges[i]) continue;
    
                context = db->bridges[i];
    
                if(context->sock == INVALID_SOCKET){
                    if(time_count > 0){
                        time_count--;
                    }else{
                        time_count = 1000;
                        now = mosquitto_time();
                    }
                    /* Want to try to restart the bridge connection */
                    if(!context->bridge->restart_t){
                        context->bridge->restart_t = now+context->bridge->restart_timeout;
                        context->bridge->cur_address++;
                        if(context->bridge->cur_address == context->bridge->address_count){
                            context->bridge->cur_address = 0;
                        }
                        if(context->bridge->round_robin == false && context->bridge->cur_address != 0){
                            context->bridge->primary_retry = now + 5;
                        }
                    }else{
                        if(context->bridge->start_type == bst_lazy && context->bridge->lazy_reconnect){
                            rc = mqtt3_bridge_connect(db, context);
                            if(rc){
                                context->bridge->cur_address++;
                                if(context->bridge->cur_address == context->bridge->address_count){
                                    context->bridge->cur_address = 0;
                                }
                            }
                        }
                        if(context->bridge->start_type == bst_automatic && now > context->bridge->restart_t){
                            context->bridge->restart_t = 0;
                            rc = mqtt3_bridge_connect(db, context);
                            if(rc == MOSQ_ERR_SUCCESS){
                                register_event(db,context->sock,EPOLLIN);
                                if(context->current_out_packet){
                                    modify_event(db,context->sock,EPOLLOUT | EPOLLIN);
                                }
                            }else{
                                /* Retry later. */
                                context->bridge->restart_t = now+context->bridge->restart_timeout;
    
                                context->bridge->cur_address++;
                                if(context->bridge->cur_address == context->bridge->address_count){
                                    context->bridge->cur_address = 0;
                                }
                            }
                        }
                    }
                }
            }
#endif
            now_time = time(NULL);
            if(db->config->persistent_client_expiration > 0 && now_time > expiration_check_time){
                HASH_ITER(hh_id, db->contexts_by_id, context, ctxt_tmp){
                    if(context->sock == INVALID_SOCKET && context->clean_session == 0){
                        /* This is a persistent client, check to see if the
                         * last time it connected was longer than
                         * persistent_client_expiration seconds ago. If so,
                         * expire it and clean up.
                         */
                        if(now_time > context->disconnect_t+db->config->persistent_client_expiration){
                            if(context->id){
                                id = context->id;
                            }else{
                                id = "<unknown>";
                            }
                            _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "Expiring persistent client %s due to timeout.", id);
#ifdef WITH_SYS_TREE
                            g_clients_expired++;
#endif
                            g_epoll_expired_fd++;
                            context->clean_session = true;
                            context->state = mosq_cs_expiring;
                            epoll_do_disconnect(db, context);
                        }
                    }
                }
                expiration_check_time = time(NULL) + 3600;
            }
    
            if(last_timeout_check < mosquitto_time()){
                /* Only check at most once per second. */
                mqtt3_db_message_timeout_check(db, db->config->retry_interval);
                last_timeout_check = mosquitto_time();
            }
            
            //清空之前的epoll事件
            /*
            if(epoll_events)
            {
                memset(epoll_events, 0, sizeof(struct epoll_event) * sock_num);
            }
            */
            //epoll监听套接字
#ifndef WIN32
            sigprocmask(SIG_SETMASK, &sigblock, &origsig);
            fdcount = epoll_wait(db->epoll_fd, epoll_events, sock_num, 100);
            sigprocmask(SIG_SETMASK, &origsig, NULL);
#else
            _mosquitto_log_printf(NULL, MOSQ_LOG_NOTICE, "mosquttio complier version is windows not linux!");
#endif

            if(fdcount == -1){
                _mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "Error in epoll_wait: %s.", strerror(errno));
            }else{
                
                if(fdcount > sock_num - 100 ){
                    sock_num *= 2;
                    epoll_events = _mosquitto_realloc(epoll_events,sizeof(struct epoll_event) * sock_num);
                    if(!epoll_events){
                        _mosquitto_log_printf(NULL, MOSQ_LOG_ERR, "[_mosquitto_realloc:%d] Error: Out of memory.",sock_num);
                        return MOSQ_ERR_NOMEM;
                    }
                }
                //epoll消息循环处理函数
                epoll_handle_event(db,epoll_events,fdcount,listensock,listensock_count);
            }

            
#ifdef WITH_PERSISTENCE
            if(db->config->persistence && db->config->autosave_interval){
                if(db->config->autosave_on_changes){
                    if(db->persistence_changes >= db->config->autosave_interval){
                        mqtt3_db_backup(db, false);
                        db->persistence_changes = 0;
                    }
                }else{
                    if(last_backup + db->config->autosave_interval < mosquitto_time()){
                        mqtt3_db_backup(db, false);
                        last_backup = mosquitto_time();
                    }
                }
            }
#endif
    
#ifdef WITH_PERSISTENCE
            if(flag_db_backup){
                mqtt3_db_backup(db, false);
                flag_db_backup = false;
            }
#endif
            if(flag_reload){
                _mosquitto_log_printf(NULL, MOSQ_LOG_INFO, "Reloading config.");
                mqtt3_config_read(db->config, true);
                mosquitto_security_cleanup(db, true);
                mosquitto_security_init(db, true);
                mosquitto_security_apply(db);
                mqtt3_log_close(db->config);
                mqtt3_log_init(db->config);
                flag_reload = false;
            }
            if(flag_tree_print){
                mqtt3_sub_tree_print(&db->subs, 0);
                flag_tree_print = false;
            }
#ifdef WITH_WEBSOCKETS
            for(i=0; i<db->config->listener_count; i++){
                /* Extremely hacky, should be using the lws provided external poll
                 * interface, but their interface has changed recently and ours
                 * will soon, so for now websockets clients are second class
                 * citizens. */
                if(db->config->listeners[i].ws_context){
                    libwebsocket_service(db->config->listeners[i].ws_context, 0);
                }
            }
            if(db->config->have_websockets_listener){
                temp__expire_websockets_clients(db);
            }
#endif
        }
    
        //关闭epoll句柄
        destory_epoll(db);
        return MOSQ_ERR_SUCCESS;

}



