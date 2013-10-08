/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 */

#include <time.h>
#include "csapp.h"
#include "sbuf.h"
#define NTHREADS  4
#define SBUFSIZE  16
#define MAX_NTHREADS 1024
#define THREAD_DEBUG 0
sbuf_t sbuf; /* shared buffer of connected descriptors */
enum BOOL {FALSE,TRUE};
typedef struct{
	pthread_t tid; // thread  tid
	int b_exit;// the tag for exit this thread, true for exit the thread, 0 for continue running
	int b_running; // business in running not on wait;
}thread_status;

sem_t strategy_mutex;       /* Notify manager thread run the strategy */
sem_t tids_status_nums_mutex; 	    /*proect tids_exit_tags and current_num_thrs*/
int current_num_thrs = NTHREADS;
thread_status *tids_exit_tags = NULL;
void doit(int fd);
void *doit_thread(void *vargp);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		char *shortmsg, char *longmsg);
void *manager_thread(void *vargp);
void manager_threads(thread_status *tid_status,int buffer_counter, int *nums_thrs);
void debug_command();
void doit_thread_cleanup(void *arg);

int main(int argc, char **argv) 
{
	int i, listenfd, connfd, port;

	thread_status tids_tags[4] = {{0,0,0}};
	socklen_t clientlen=sizeof(struct sockaddr_in);
	struct sockaddr_in clientaddr;
	pthread_t tid;
	fd_set read_set, ready_set;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}
	(void)signal(SIGPIPE, SIG_IGN); /* ignore connection disconnected by remote signal */
	port = atoi(argv[1]);
	sbuf_init(&sbuf, SBUFSIZE); //line:conc:pre:initsbuf
	Sem_init(&strategy_mutex, 0, 1);      /* Binary semaphore for locking */
	Sem_init(&tids_status_nums_mutex, 0, 1);      /* Binary semaphore for locking */

	listenfd = Open_listenfd(port);
	FD_ZERO(&read_set);
	FD_SET(STDIN_FILENO,&read_set);
	FD_SET(listenfd,&read_set);

	Pthread_create(&tid, NULL, manager_thread, NULL);               //line:conc:pre:endcreate
	for (i = 0; i < NTHREADS; i++)  /* Create worker threads */ //line:conc:pre:begincreate
		Pthread_create(&tid, NULL, doit_thread, &tids_tags[i]);               //line:conc:pre:endcreate

	while (1) {
		ready_set = read_set;
		Select(listenfd+1,&ready_set,NULL,NULL,NULL);
		if(FD_ISSET(STDIN_FILENO,&ready_set))
		{
#ifdef THREAD_DEBUG
			printf("command enter:\n");
#endif
			debug_command();

		}
		if(FD_ISSET(listenfd,&ready_set))
		{
			clientlen = sizeof(clientaddr);
			connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
			sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */
			V(&strategy_mutex);
		}
	}
}
/* $end tinymain */



void *manager_thread(void *vargp)
{
	tids_exit_tags = (thread_status*) Malloc(sizeof(thread_status)*MAX_NTHREADS);
	memset(tids_exit_tags,0,sizeof(thread_status)*MAX_NTHREADS);
	pthread_t self_tid = pthread_self();
	Pthread_detach(self_tid);
	int vCounter = 0;
	struct timespec ts;
	while(1)
	{
		if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
		               printf("clock_gettime");
		ts.tv_sec += 10;
		sem_timedwait(&strategy_mutex, &ts);
		Sleep(1);//selp half second and let cpu go others threads
		if(++vCounter == SBUFSIZE)
		{
			vCounter = 0;//reset the counter
			int item_counter = sbuf_item_counter(&sbuf);// check whether empty or full
			manager_threads(tids_exit_tags, item_counter, &current_num_thrs);
		}
	}
	free(tids_exit_tags);

}

/**
 * manager_threads
 * increase 4 when the buffer becomes full,
 * or decrease the number of threads when the buffer becomes empty step by step
 */
void manager_threads(thread_status *tid_status,int buffer_counter, int *nums_thrs)
{
	int i = 0;
	int l_nums_thread = *nums_thrs;
	int num_threads_changed = 0;
	int num_threads_not_working = 0;
	printf("manager_threads buffer_counter %d,nums_thrs %d, tid_status %p \n",buffer_counter, (*nums_thrs), tid_status);
	P(&tids_status_nums_mutex);
	if( buffer_counter == 0 && *nums_thrs > NTHREADS)
	{// kill threads
		i = 0;
		while(i < MAX_NTHREADS)
		{
			i++;
			if(tid_status[i].tid && !tid_status[i].b_exit)
			{
				if(!tid_status[i].b_running)
				{
  				   if(0 != pthread_cancel(tid_status[i].tid))
				   {
				 	printf("pthread_cancel error!");
  				   }
				   tid_status[i].b_exit = TRUE;
 			           //decrease the number of thread by 4 one time
				  if(num_threads_changed++ > NTHREADS )
				   {
				    break;
 				   } 
				}

			}
		}
	}else if( buffer_counter == SBUFSIZE )
	{// increase threads
		i = 0;

		while(i < MAX_NTHREADS)
		{
			i++;
			if( tid_status[i].tid == 0 && sbuf_item_counter(&sbuf) == SBUFSIZE )
			{
				tid_status[i].b_exit = FALSE;
				tid_status[i].b_running = FALSE;
				Pthread_create(&(tid_status[i].tid), NULL, doit_thread, &tid_status[i]);               //line:conc:pre:endcreate
				printf("Create tid %08x \n", tid_status[i].tid);
				//increate the number of thread by NTHREADS one time
				if(num_threads_changed++ > NTHREADS )
				{
					break;
				}
				// let CPU go to others threads after create one thread
				sleep(1);
			}else if(tid_status[i].tid && !tid_status[i].b_running)
			{
				num_threads_not_working++;
			}
		}
		sleep(4);
		*nums_thrs += num_threads_changed;
	}
	V(&tids_status_nums_mutex);
}

void debug_command()
{
	char buf[MAXLINE];
	if(!Fgets(buf, MAXLINE,stdin))
		exit(0);
	int i = 0;
	printf("current_num_thrs %d\n", current_num_thrs);
	for(i = 0; i < MAX_NTHREADS; i++)
	{
		if(tids_exit_tags[i].tid)
		{
			printf("%d\ttid %08x\t running %d\t b_exit %d\n",i,tids_exit_tags[i].tid,tids_exit_tags[i].b_running,
					tids_exit_tags[i].b_exit);
		}
	}
}

/**
 * doit_thread_cleanup
 * run doit_thread_cleanup when doit_thread exit
 * purpose:
 *    1)reset tid array
 *    2)decrease current_num_thrs atomic
 */
void doit_thread_cleanup(void *arg)
{
	P(&tids_status_nums_mutex);
	printf("doit_thread_cleanup: 0x%p\n",arg);
	thread_status* tid = (thread_status*)arg;
	tid->tid = 0;
	tid->b_exit = 0;
	tid->b_running = FALSE;
	current_num_thrs--;
	V(&tids_status_nums_mutex);
}
/**
 * tiny web server services thread
 * 1)get description from buffer
 * 2)set thread cancel disable
 * 3)services the request
 * 4)set thread cancel enable
 *
 */
void *doit_thread(void *vargp) 
{ 
	pthread_cleanup_push(doit_thread_cleanup,vargp);
	thread_status* tid = (thread_status*)vargp;
	pthread_t self_tid = pthread_self();
	Pthread_detach(self_tid);
	
	while (1) {

		tid->b_running = FALSE;
		int connfd = sbuf_remove(&sbuf); /* Remove connfd from buffer */ //line:conc:pre:removeconnfd
		tid->b_running = TRUE;
		
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		doit(connfd); 
		Close(connfd);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		if(sbuf_item_counter(&sbuf) == 0 && tid->b_exit)
		{
			break;
		}
	}
	pthread_cleanup_pop(1);

}

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;
  
    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);                   //line:netp:doit:readrequest
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
       clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr
    read_requesthdrs(&rio);                              //line:netp:doit:readrequesthdrs

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);       //line:netp:doit:staticcheck
    if (stat(filename, &sbuf) < 0) {                     //line:netp:doit:beginnotfound
	clienterror(fd, filename, "404", "Not found",
		    "Tiny couldn't find this file");
	return;
    }                                                    //line:netp:doit:endnotfound

    if (is_static) { /* Serve static content */          
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { //line:netp:doit:readable
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't read the file");
	    return;
	}
	serve_static(fd, filename, sbuf.st_size);        //line:netp:doit:servestatic
    }
    else { /* Serve dynamic content */
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { //line:netp:doit:executable
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't run the CGI program");
	    return;
	}
	serve_dynamic(fd, filename, cgiargs);            //line:netp:doit:servedynamic
    }
}
/* $end doit */

/*
 * read_requesthdrs - read and parse HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];
    int head_nums = 0;
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s", buf);
	if(head_nums++ > MAXLINE)
		break;
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) {  /* Static content */ //line:netp:parseuri:isstatic
	strcpy(cgiargs, "");                             //line:netp:parseuri:clearcgi
	strcpy(filename, ".");                           //line:netp:parseuri:beginconvert1
	strcat(filename, uri);                           //line:netp:parseuri:endconvert1
	if (uri[strlen(uri)-1] == '/')                   //line:netp:parseuri:slashcheck
	    strcat(filename, "home.html");               //line:netp:parseuri:appenddefault
	return 1;
    }
    else {  /* Dynamic content */                        //line:netp:parseuri:isdynamic
	ptr = index(uri, '?');                           //line:netp:parseuri:beginextract
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	}
	else 
	    strcpy(cgiargs, "");                         //line:netp:parseuri:endextract
	strcpy(filename, ".");                           //line:netp:parseuri:beginconvert2
	strcat(filename, uri);                           //line:netp:parseuri:endconvert2
	return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
    /* Send response headers to client */
    get_filetype(filename, filetype);       //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.0 200 OK\r\n");    //line:netp:servestatic:beginserve
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));       //line:netp:servestatic:endserve

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);    //line:netp:servestatic:open
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);//line:netp:servestatic:mmap
    Close(srcfd);                           //line:netp:servestatic:close
    Rio_writen(fd, srcp, filesize);         //line:netp:servestatic:write
    Munmap(srcp, filesize);                 //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    else
	strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* child */ //line:netp:servedynamic:fork
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
	Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
	Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
