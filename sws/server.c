#include "server.h"

bool
set_config_ipaddress(char *ipaddress)
{
	if (ipaddress == NULL) {
		CONFIG.ipaddress = NULL;
	}
	else {
		if (is_valid_ipv4(ipaddress) || is_valid_ipv6(ipaddress)) {
			if (safe_realloc(&CONFIG.ipaddress, 1, strlen(ipaddress) + 1) != 0)
				return false;
			(void)strcpy(CONFIG.ipaddress, ipaddress);
		}
	}
	return true;
}

bool
set_config_portnumber(char *portnumber)
{
	if (portnumber == NULL) {
		CONFIG.portnumber = DEFAULT_PORT;
	}
	else {
		if (is_valid_portnumber(portnumber)) {
			if (safe_realloc(&CONFIG.portnumber, 1, strlen(portnumber) + 1) != 0)
				return false;
			(void)strcpy(CONFIG.portnumber, portnumber);
		}
	}
	return true;
}

bool 
set_config_cgidir(char *cgidir)
{
	if (cgidir == NULL) {
		CONFIG.cgidir = NULL;
	}
	else {
		char resolvedpath[MAX_PATH_SIZE];
		if (realpath(cgidir, resolvedpath) == NULL)
			return false;
		if (is_valid_dir(resolvedpath)) {
			if (safe_realloc(&CONFIG.cgidir, 1, strlen(resolvedpath) + 1) != 0)
				return false;
			(void)strcpy(CONFIG.cgidir, resolvedpath);			
		}
	}
	return true;
}

bool
set_config_rootdir(char *rootdir)
{
	if (rootdir == NULL)
		return false;
	char resolvedpath[MAX_PATH_SIZE];
	if (realpath(rootdir, resolvedpath) == NULL)
		return false;
	if (is_valid_dir(resolvedpath)) {
		if (safe_realloc(&CONFIG.rootdir, 1, strlen(resolvedpath) + 1) != 0)
			return false;
		(void)strcpy(CONFIG.rootdir, resolvedpath);
		if (chdir(CONFIG.rootdir) == -1)
			return false;
		return true;
	}
	return false;
}

bool
set_config_logdir(char *logdir)
{
	if (logdir == NULL) {
		CONFIG.logdir = NULL;
	}
	else {
		char resolvedpath[MAX_PATH_SIZE];
		if (realpath(logdir, resolvedpath) == NULL)
			return false;
		if (get_file_stat(resolvedpath) == 1) {
			(void)strcat(resolvedpath, "/");
			(void)strcat(resolvedpath, DEFAULT_LOG_NAME); 
		}
		if (safe_realloc(&CONFIG.logdir, 1, strlen(resolvedpath) + 1) != 0)
			return false;
		(void)strcpy(CONFIG.logdir, resolvedpath);
		if (!init_log())
			return false;
	}
	return true;
}

bool
insert_server_listentable(struct simpleserver *server, int listenfd)
{
	if (server == NULL)
		return false;
	uintmax_t maxsocketnum;
	if (!get_max_socketnumber(&maxsocketnum))
		return false;
	assert(listenfd <= (int)maxsocketnum);
	if (server->lcount < (int)maxsocketnum) {
		server->ltable[server->lcount] = listenfd;
		++server->lcount;		
	}
	return true;
}

bool 
remove_server_listentable(struct simpleserver *server, int listenfd)
{
	if (server == NULL)
		return false;
	for (int n = 0; n != server->lcount; ++n) {
		if (server->ltable[n] == listenfd) {
			close(server->ltable[n]);
			server->ltable[n] = 0;
			--server->lcount;
			break;
		}
	}
	return true;
}

bool
insert_server_connectiontable(struct simpleserver *server, int pollfd, int connfd, struct sockaddr *address)
{
	if (server == NULL)
		return false;
	uintmax_t maxsocketnum;
	if (!get_max_socketnumber(&maxsocketnum))
		return false;
	assert(connfd <= (int)maxsocketnum);
	if (server->ccount < (int)maxsocketnum) {
		++server->ccount;
		if (!init_httpconnection(&server->ctable[connfd], pollfd, connfd, address)) {
			close_httpconnection(&server->ctable[connfd]);
			return false;
		}
	}
	return true;
}

bool 
remove_server_connectiontable(struct simpleserver *server, int connfd)
{
	if (server == NULL)
		return false;
	close_httpconnection(&server->ctable[connfd]);
	(void)memset(&server->ctable[connfd], 0, sizeof(struct httpconnection));
	--server->ccount;
	return true;
}

bool
init_simpleserver(struct simpleserver * server, char *ipaddress, 
	char *portnumber, bool debugmode, bool cgienabled, char *cgidir, char *rootdir, char *logdir)
{
	if (server == NULL)
		return false;
	(void)memset(server, 0, sizeof(struct simpleserver));
	if (!set_config_ipaddress(ipaddress))
		return false;
	if (!set_config_portnumber(portnumber))
		return false;
	if (!set_config_cgidir(cgidir))
		return false;
	if (!set_config_rootdir(rootdir))
		return false;
	if (!set_config_logdir(logdir))
		return false;
	uintmax_t maxsocketnum;
	if (!get_max_socketnumber(&maxsocketnum))
		return false;
	if (safe_realloc(&server->ltable, maxsocketnum, sizeof(int)) != 0)
		return false;
	(void)memset(server->ltable, 0, maxsocketnum * sizeof(int));
	if (safe_realloc(&server->ctable, maxsocketnum, sizeof(struct httpconnection)) != 0)
		return false;
	(void)memset(server->ctable, 0, maxsocketnum * sizeof(struct httpconnection));
	if (!init_threadpool(&server->pool, DEFAULT_THREAD_NUM, DEFAULT_CONN_NUM))
		return false;
	CONFIG.debugmode = debugmode;
	CONFIG.cgienabled = cgienabled;
	CONFIG.protocolver = DEFAULT_PROTOCOL_NAME;
	CONFIG.servername = DEFAULT_SERVER_NAME;
	return true;
}

int
get_tcpaddrs(struct simpleserver *server, struct addrinfo **reslist) {
	if (server == NULL)
		return -1;
	struct addrinfo hints;	
	(void)memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
	return getaddrinfo(CONFIG.ipaddress, CONFIG.portnumber, &hints, reslist);
}

int
listen_connections(struct simpleserver *server, struct addrinfo *reslist) {
	if (server == NULL)
		return -1;
	struct addrinfo *p;
	for (p = reslist; p != NULL; p = p->ai_next) {
//#ifdef DEBUG
//		char addrdst[20];
//		if (p->ai_family == AF_INET) {
//			inet_ntop(p->ai_family, &((struct sockaddr_in *)p->ai_addr)->sin_addr, addrdst, 20);
//			printf("IP: %s\n", addrdst);
//			printf("Port: %u\n", ntohs(((struct sockaddr_in *)p->ai_addr)->sin_port));
//		}
//		if (p->ai_family == AF_INET6) {
//			inet_ntop(p->ai_family, &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr, addrdst, 20);
//			printf("IP: %s\n", addrdst);
//			printf("Port: %u\n", ntohs(((struct sockaddr_in6 *)p->ai_addr)->sin6_port));
//		}	  
//#endif // DEBUG
		int listenfd, optval, backlog;
		optval = 1;
		backlog = CONFIG.debugmode ? 1 : MAX_LISTEN;
		
		listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listenfd < 0)
			continue;
		if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int)) < 0)
			continue;
		if (bind(listenfd, p->ai_addr, p->ai_addrlen) != 0) {
			close(listenfd);
			continue;
		}
		if (listen(listenfd, backlog) < 0) {
			close(listenfd);
			continue;
		}
		if (!insert_server_listentable(server, listenfd)) {
			remove_server_listentable(server, listenfd);
			continue;
		}
	}
	return 0;
}

int
accept_connections(struct simpleserver *server)
{
	if (server == NULL)
		return -1;
	uintmax_t maxsocketnum;
	if (!get_max_socketnumber(&maxsocketnum))
		return -2;
#ifdef _BSD_
	struct kevent events[maxsocketnum];
	int kqueuefd = kqueue();
	for (int n = 0; n != server->lcount; ++n)
		add_fd(kqueuefd, server->ltable[n], false);
	for (;;) {
		int number = kevent(kqueuefd, NULL, 0, events, maxsocketnum, NULL);
		if ((number < 0) && (errno != EINTR))
			break;
		for (int n = 0; n < number; ++n) {
			int sockfd = events[n].ident;
			if (contain_fd(sockfd, server->ltable, server->lcount)) {
				struct sockaddr_storage client_address;
				socklen_t client_addrlength = sizeof(struct sockaddr_storage);
				int connfd = accept(sockfd, (struct sockaddr*)&client_address, &client_addrlength);
				if (connfd < 0)
					continue;
				if (server->ccount == (int)maxsocketnum)
					continue;
				(void)insert_server_connectiontable(server, kqueuefd, connfd, (struct sockaddr*)&client_address);
				(void)add_fd(kqueuefd, connfd, true);
			}
			else if (events[n].flags & (EV_EOF | EV_ERROR)) {
				(void)remove_server_connectiontable(server, sockfd);
				(void)remove_fd(kqueuefd, sockfd);
			}
			else if (events[n].filter == EVFILT_READ) {
				if (read_httpconnection(&server->ctable[sockfd])) {
					(void)append_threadpool(&server->pool, &server->ctable[sockfd]);
					continue;
				}
				else {
					(void)remove_server_connectiontable(server, sockfd);
					(void)remove_fd(kqueuefd, sockfd);
				}
			}
			else if (events[n].filter == EVFILT_WRITE) {
				(void)write_httpconnection(&server->ctable[sockfd]);
				(void)remove_server_connectiontable(server, sockfd);
				(void)remove_fd(kqueuefd, sockfd);
			}
			else
				continue;
		}
	}
#else
	struct epoll_event events[maxsocketnum];
	int epollfd = epoll_create(5);
	for (int n = 0; n != server->lcount; ++n)
		add_fd(epollfd, server->ltable[n], false);
	for (;;) {
		int number = epoll_wait(epollfd, events, maxsocketnum, -1);
		if ((number < 0) && (errno != EINTR))
			break;
		for (int n = 0; n < number; ++n) {
			int sockfd = events[n].data.fd;
			if (contain_fd(sockfd, server->ltable, server->lcount)) {
				struct sockaddr_storage client_address;
				socklen_t client_addrlength = sizeof(struct sockaddr_storage);
				int connfd = accept(sockfd, (struct sockaddr*)&client_address, &client_addrlength);
				if (connfd < 0)
					continue;
				if (server->ccount == (int)maxsocketnum)
					continue;
				insert_server_connectiontable(server, epollfd, connfd, (struct sockaddr*)&client_address);
				add_fd(epollfd, connfd, true);
			}
			else if (events[n].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
				remove_server_connectiontable(server, sockfd);
				remove_fd(epollfd, sockfd);
			}
			else if (events[n].events & EPOLLIN) {
				if (read_httpconnection(&server->ctable[sockfd])) {					
					append_threadpool(&server->pool, &server->ctable[sockfd]);
//					process(&server->ctable[sockfd]); 
					continue;
				}
				else {
					remove_server_connectiontable(server, sockfd);
					remove_fd(epollfd, sockfd);
				}
			}
			else if (events[n].events & EPOLLOUT) {
				(void)write_httpconnection(&server->ctable[sockfd]);
				remove_server_connectiontable(server, sockfd);
				remove_fd(epollfd, sockfd);
			}
			else
				continue;
		}
	}
#endif // _BSD_
	
		return 0;
}

int
setup_server(struct simpleserver *server) {
	if (server == NULL)
		return -1;
	add_sig(SIGPIPE, SIG_IGN, true);
	struct addrinfo *reslist;
	if (get_tcpaddrs(server, &reslist) != 0) 
		return -2;
	if (listen_connections(server, reslist) != 0) 
		return -3;
	if (accept_connections(server) != 0) 
		return -4;
	//might leak here.
	freeaddrinfo(reslist);
	return 0;
}

void
close_server(struct simpleserver *server)
{
	for (int n = 0; n != server->lcount; ++n)
		(void)remove_server_listentable(server, server->ltable[n]);
	uintmax_t maxsocketnum;
	(void)get_max_socketnumber(&maxsocketnum);
	for (int n = 0; n != (int)maxsocketnum; ++n)
		(void)remove_server_connectiontable(server, maxsocketnum);
	destroy_threadpool(&server->pool);
	destroy_log();
	free(CONFIG.ipaddress);
	free(CONFIG.portnumber);
	free(CONFIG.cgidir);
	free(CONFIG.rootdir);
	free(CONFIG.logdir);
	free(server->ltable);
	free(server->ctable);
}