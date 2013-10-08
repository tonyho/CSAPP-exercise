/* Name: Lalitha Ganesan
 * Andrew ID: ltg
 * Thread-Safe Caching Web Proxy 
 * 
 * My proxy operates by:
 * 1. Open a listening socket (using open_listenfd)
 * 2. Accept incoming requests from clients (using accept)
 * 3. Check if it's a GET request: if no, exit
 * 4. If yes, parse the request (of the form: http://hostname:port/filename)to get hostname, port number, and filepath
 * 5. Build a HTTP response (of the form: GET filepath HTTP/1.0\r\n)
 * 6. Open a connection to the server specified in the URL (using open_clientfd)
 * 7. Forward the GET request to the server
 * 8. Read all subsequent lines (using readline) in the request from the browser and forward to server -> empty line marks end of request
 * 9. In a loop, read response from the server, print to screen, and forward it to the browser (using read)
 *
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csapp.h"

#define DEFAULT_HTTP_PORT 80

/* Port assigned to me = 9708 */
#define DEFAULT_PORT 9708

#define MAX_BUFFER_SIZE 10000
#define DEFAULT_BUFFER_SIZE 512

#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_SIZE 1048574

/* Cache structs */
typedef struct cache_object 
{
	char *response;
	char *uri;
	time_t timestamp;
	int size;
	struct cache_object *next;
	struct cache_object *prev;
} cache_object;

typedef struct cache 
{
	struct cache_object *head;
	pthread_rwlock_t lock;
	int cache_size;
} cache;

/* Function prototypes */

void *startRequest(void *vargp); /* Handles request from client */

/* Gets the information from the client request */
void getHostname(char *request, char **hostname);
void getFilepath(char *request, char **filepath);
void getHeaders(char *request, char **headers);
int getPort(char * request);
void getUri(char *request, char **uri);

/* Sends request to the server */
int sendRequest(int connfd, char *filepath, char *headers, size_t size);

/* Makes sure the request is only a GET request */
int isGET(char *request);
void cleanPtrs(char *ptr1, char *ptr2, char *ptr3, char *ptr4, char *ptr5, char *ptr6, char *ptr7, int connfd1, int connfd2);

/* Terminates proxy when Ctrl-C'd */
void sigint_handler(int signum);

/* Locks to deal with concurrency issues */
void write_lock(void);
void read_lock(void);
void unlock(void);

/* Cache functions */
void initCache(void);
void evict(void);
void addToCache(char *response, char *uri, size_t size); 
void updateTime(cache_object *object);
int checkCacheSize(size_t size);
int checkCache(char *uri);
void getFromCache(int clientfd, char *uri);
void removeFromList(cache_object *object);

/* Global var - Cache struct variable*/
static cache *mycache;


int main (int argc, char *argv []) 
{
	int listenfd, port;
	socklen_t clientlen = sizeof(struct sockaddr_in);
	int *connfdp = NULL;
	struct sockaddr_in clientaddr;
	pthread_t tid;

	/* Check for port arg */
	if(argc != 2) 
	{
		fprintf(stderr, "Usage: ./proxy <port>\n");
		exit(1);
	} 
	port = atoi(argv[1]);
	
	/* Check port range */
	if(port < 0 || port > 65535) 
	{
	fprintf(stderr, "Port out of range: Choose a port from 0 to 65535 inclusive\n");
		exit(-1);
	}

	/* Initialize cache */
	initCache();

	/* Open port to listen */
	if((listenfd = open_listenfd(port)) < 0) 
	{
		fprintf(stderr, "Sorry, cannot open socket on port %d to listen.\n", port);
		exit(-1);
	}

	/* Setup signal handlers to handle the SIGPIPE signal */
	Signal(SIGPIPE, SIG_IGN);
	Signal(SIGINT, sigint_handler);

	/* Start proxy  */
	while(1) 
	{
		connfdp = Malloc(sizeof(int));
		*connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Pthread_create(&tid, NULL, startRequest, connfdp);
	}
}

void *startRequest(void *vargp) 
{
	/* Initialize the strings for the user input, parsed request, and response to server */
	char *buffer = Calloc(sizeof(char), MAX_BUFFER_SIZE);
	char *request = Calloc(sizeof(char), MAX_BUFFER_SIZE);
	char *response = Calloc(sizeof(char), MAX_BUFFER_SIZE);

	/* The components of the request */
	char *filepath = NULL; 
	char *hostname = NULL;
	char *headers = NULL;
	char *uri = NULL;

	rio_t clientrio;

	/* File descriptors */
	int clientfd = -1;
	int serverfd = -1;

	int bytecounter;
	int serverport;
	int notcacheable = 0;
	
	/* Set the maximum request and response size */
	size_t max_request_size = MAX_BUFFER_SIZE;
	size_t max_response_size = MAX_BUFFER_SIZE;

	size_t cur_request_size = 0;
	size_t cur_response_size = 0;

	/* Get connfd out of the parameter var and free its allocated memory */
	clientfd = *((int *)vargp);
	Free(vargp);

	/* Detach thread */
	Pthread_detach(Pthread_self());

	/* Initialize client rio */
	Rio_readinitb(&clientrio, clientfd);

	/* Read in request from client */
	while((bytecounter = rio_readlineb(&clientrio, ((void *) buffer), MAX_BUFFER_SIZE)) > 0) 
	{
		/* If we didn't read anything, clean ptrs and exit thread */
		if(bytecounter == -1) 
		{
			cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
			Pthread_exit(NULL);

		} 
		/* If we've filled up the request size, realloc to double the size */
		else if((bytecounter + cur_request_size + 1) > ((int) max_request_size)) 
		{
			request = Realloc(request, max_request_size * 2);
			max_request_size *= 2; // Double the max request size
			strncat(request, buffer, bytecounter); // Adds bytecounter bytes of buffer to request

		} 
		/* At the end of the request, calculate the request size and add bytecounter bytes of buffer to request */
		else if(strcmp(buffer, "\r\n") == 0) 
		{
			cur_request_size += bytecounter;
			strncat(request, buffer, bytecounter);
			break;

		} 
		/* Else, just copy buffer to request */
		else 
		{
			strncat(request, buffer, bytecounter);
		}

		/* Update request size and clear the buffer */
		cur_request_size += bytecounter;
		bzero(buffer, MAX_BUFFER_SIZE);
	}

	/* Make sure this is a GET request. If not, clean ptrs and exit thread. */
	if(isGET(request) < 0) 
	{
		fprintf(stderr, "The proxy only supports GET requests.\n");
		cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
		Pthread_exit(NULL);
	}

	/* Parse out components of the request */
	getHostname(request, &hostname);
	serverport = getPort(request);
	getFilepath(request, &filepath);
	getHeaders(request, &headers);
	getUri(request, &uri);

	/* Is the request is already in the cache? */
	if(checkCache(uri)) 
	{
		/* If it's in the cache */
		getFromCache(clientfd, uri);
		cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
		Pthread_exit(NULL);
	}

	/* If it's not in the cache, need to open a connection to the server */
	serverfd = open_clientfd(hostname, serverport);

	/* Check server connection */
	if(serverfd < 0) 
	{
		/* If server connection opening failed, clean ptrs and exit thread */
		fprintf(stderr, "Sorry, the proxy can't connect to server.\n");
		cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
		Pthread_exit(NULL);
	}

	/* Send request to the server */
	if(sendRequest(serverfd, filepath, headers, cur_request_size) < 0) 
	{
		/* If sending request to the server fails, clean ptrs and exit thread */
		fprintf(stderr, "Sorry, the proxy can't send request to server.\n");
		cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
		Pthread_exit(NULL);
	}
	/* Clear the buffer */
	bzero(buffer, MAX_BUFFER_SIZE);

	/* Read response back from server */
	while((bytecounter = rio_readn(serverfd, ((void *) buffer), MAX_BUFFER_SIZE)) > 0) 
	{
		/* If reading failed, clean ptrs and exit thread */
		if(bytecounter == -1) 
		{
			fprintf(stderr, "Sorry, the proxy can't read server's response.\n");
			cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
			Pthread_exit(NULL);
		} 
		/* If requested object is too big for cache size max, clean ptrs and exit thread */
		else if(notcacheable) 
		{
			if(rio_writen(clientfd, buffer, bytecounter) < 0) 
			{
				cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
				Pthread_exit(NULL);
			}
			continue;

		} 
		/* If we need to resize the response, realloc to double the size and copy buffer */
		else if(bytecounter + cur_response_size + 1 > (int) max_response_size) 
		{
			response = Realloc(response, max_response_size * 2);
			max_response_size *= 2;
			memcpy(response + cur_response_size, buffer, bytecounter);

		} 
		/* Else, just copy buffer */
		else 
		{
			memcpy(response + cur_response_size, buffer, bytecounter);
		}

		/* Update response size */
		cur_response_size += bytecounter;


		/* Check that our response size is below cache max object size */
		/* If response size is too big, clean ptrs and exit thread */
		if((cur_response_size > MAX_OBJECT_SIZE) && (notcacheable == 0)) 
		{
			notcacheable = 1;

			if(rio_writen(clientfd, response, cur_response_size) < 0) 
			{
				cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
				Pthread_exit(NULL);
			}
		}
		/* We're safe to cache it */
		if(bytecounter < MAX_BUFFER_SIZE) 
		{
			break;
		}
	}


	/* If requested obj is small enough to put into cache */
	if(notcacheable == 0) 
	{

		/* Write cur response to client */
		if(rio_writen(clientfd, response, cur_response_size) < 0) 
		{
			cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
			Pthread_exit(NULL);
		}

		/* Add to the cache */
		addToCache(response, uri, cur_response_size);
	}

	/* Clean up pointers and exit thread */
	cleanPtrs(buffer, request, response, filepath, hostname, headers, uri, clientfd, serverfd);
	Pthread_exit(NULL);

	/* Done with this request, so exit the program */
	exit(0);
}


/* getHostName - 
 *
 * Parse out hostname from request and store in hostname
 */
void getHostname(char *request, char **hostname) 
{
	char *localrequest = Calloc(sizeof(char), strlen(request) + 1);
	char *localstart = localrequest;

	/* Get first line from request */
	strncpy(localrequest, request, (int)(index(request, '\n') - request) + 1);

	/* Hostname comes after two /'s and before : or after the three /'s */
	strsep(&localrequest, "/");
	strsep(&localrequest, "/");
	localrequest = strsep(&localrequest, "/");

	if(localrequest != NULL && index(localrequest, ':') != NULL) 
	{
		localrequest = strsep(&localrequest, ":");
	}

	/* Copy, then free */
	*hostname = Calloc(sizeof(char), strlen(localrequest) + 1);
	strncpy(*hostname, localrequest, strlen(localrequest) + 1);
	Free(localstart);
}

/* getFilepath - 
 * Extract filepath from request and store in filepath
 */
void getFilepath(char *request, char **filepath) 
{
	char *localrequest = Calloc(sizeof(char), strlen(request) + 1);
	char *localstart = localrequest;

	/* Get first line from request */
	strncpy(localrequest, request, (int)(index(request, '\n') - request) + 1);

	/* Get uri */
	strsep(&localrequest, " ");
	localrequest = strsep(&localrequest, " ");
	localrequest = strsep(&localrequest, " ");

	/* Filepath comes after three /'s */
	strsep(&localrequest, "/");
	strsep(&localrequest, "/");
	strsep(&localrequest, "/");

	/* Copy, then free */
	*filepath = Calloc(sizeof(char), strlen(localrequest) + 1);
	strncpy(*filepath, localrequest, strlen(localrequest) + 1);
	Free(localstart);
}

/* getUri - 
 * Get URI out of request 
*/
void getUri(char *request, char **uri) 
{
	char *localrequest;
	char *localstart;

	localrequest = Calloc(strlen(request) + 1, sizeof(char));
	strcpy(localrequest, request);
	localstart = localrequest;
	strsep(&localrequest, " ");
	localrequest = strsep(&localrequest, " ");

    	*uri = Calloc(strlen(localrequest) + 1, sizeof(char));
	strcpy(*uri, localrequest);
    
	Free(localstart);
}

/* getPort - 
 * Extract server port from request and return it
 */
int getPort(char *request) 
{
	char *localrequest = Calloc(sizeof(char), strlen(request) + 1);
	char *localstart = localrequest;

	/* Get first line from request */
	strncpy(localrequest, request, (int)(index(request, '\n') - request) + 1);

	/* Port comes after after two /'s, and : and before / */
	strsep(&localrequest, "/");
	strsep(&localrequest, "/");
	localrequest = strsep(&localrequest, "/");
	strsep(&localrequest, ":");

	/* Free up */
	Free(localstart);

	/* Return port if it exists */
	if(localrequest == NULL) 
	{
		return DEFAULT_HTTP_PORT;
	} 
	else 
	{
		return atoi(localrequest);
	}
}

/* getHeaders - 
 * Get headers from request, manipulate to fit needs for proxy,
 * and store in headers.
 */
void getHeaders(char *request, char **headers) 
{
	char *localrequest = Calloc(sizeof(char), strlen(request) + 1);
	char *localstart = localrequest;
	char *currentheader = NULL;
	size_t buffersize;
	size_t cursize;

	/* Copy entire request */
	strncpy(localrequest, request, strlen(request) + 1);

	/* Get everything after first line */
	strsep(&localrequest, "\n");

	/* Allocate room for headers */
	*headers = Calloc(sizeof(char), strlen(localrequest) + 1);

	/* Create buffer */
	buffersize = strlen(localrequest) + 1;

	/* Initialize size */
	cursize = 0;

	/* Read in headers line by line and change the ones we need to change */
	while((currentheader = strsep(&localrequest, "\n")) != NULL) 
	{
		if(strlen(currentheader) + 3 + cursize > buffersize) 
		{
			Realloc(*headers, buffersize * 2);
			buffersize *= 2;
		}

		/* We're done reading */
		if(strlen(currentheader) == 0) 
		{
			break;
		}

		if(strspn("Proxy-Connection:", currentheader) == strlen("Proxy-Connection:")) 
		{
			strcat(*headers, "Proxy-Connection: close\r\n");
		} 
		else if(strspn("Connection:", currentheader) == strlen("Connection:")) 
		{
			strcat(*headers, "Connection: close\r\n");
		} 
		//else if(strspn("Keep-Alive:", currentheader) == strlen("Keep-Alive:")) 
		//{
			//break;
		//} 
		else 
		{
			strcat(*headers, currentheader);
			strcat(*headers, "\n");
		}
	}
	Free(localstart);
}

/* sendRequest - 
 * Send request to server 
*/
int sendRequest(int serverfd, char *filepath, char* headers, size_t size) 
{
	/* Initializes request buffer to send to server */
	char *requestbuf = Calloc(sizeof(char), size + 1);
	
	/* Copies request info into requestbuf */
	sprintf(requestbuf, "GET /%s HTTP/1.0\r\n", filepath);
	sprintf(requestbuf, "%s%s", requestbuf, headers);
	
	/* or debugging purposes */
	printf("%s\n", requestbuf);
	
	/* Write the request to the server file descriptor to send to server */
	if(rio_writen(serverfd, requestbuf, strlen(requestbuf)) < 0) 
		return -1;

	Free(requestbuf);
	return 0;
}

/* isGET - 
 * Make sure the method is GET 
*/
int isGET(char *request) 
{	
	char *method = calloc(sizeof(char), 4);

	/* Check the first 3 characters to make sure they are "GET" */
	strncpy(method, request, 3);

	if(strcmp(method, "GET") != 0) 
	{
		/* It's not GET */
		fprintf(stderr, "The request is not GET : %s\n", method);
		Free(method);
		return -1;
	} 
	else 
	{
		/* It is GET */
		Free(method);
		return 0;
	}
}

/* cleanPtrs - 
 * Free the seven pointers, and close two fd's 
*/
void cleanPtrs(char *ptr1, char *ptr2, char *ptr3, char *ptr4, char *ptr5, char *ptr6, char *ptr7, int connfd1, int connfd2) 
{
	if(ptr1 != NULL) 
	{
		Free(ptr1);
	}

	if(ptr2 != NULL) 
	{
		Free(ptr2);
	}

	if(ptr3 != NULL) 
	{
		Free(ptr3);
	}

	if(ptr4 != NULL) 
	{
		Free(ptr4);
	}

	if(ptr5 != NULL) 
	{
		Free(ptr5);
	}

	if(ptr6 != NULL) 
	{
		Free(ptr6);
	}

	if(ptr7 != NULL) 
	{
		Free(ptr7);
	}

	if(connfd1 >= 0) 
	{
		Close(connfd1);
	}

	if(connfd2 >= 0) 
	{
		Close(connfd2);
	}
}

/* sigint_handler - 
 * Free up cache on SIGINT - after Ctrl-C 
*/
void sigint_handler(int signum) 
{
	cache_object *cur = mycache->head;

	printf("Terminating proxy.\n");

	/* Clean up cache */
	while(cur) 
	{
		if(cur->prev) 
		{
			free(cur->prev);
		}

		free(cur->response);
		free(cur->uri);

		cur = cur->next;
	}
	exit(0);
}


/* Cache functions */

/* initCache - 
 *
*/
void initCache(void) 
{
	/* Create cache object */
	mycache = Calloc(1, sizeof(cache));

	/* Set head to NULL */
	mycache->head = NULL;

	/* Init cache rw_lock */
	if(pthread_rwlock_init(&(mycache->lock), NULL) != 0) 
	{
		fprintf(stderr, "Couldn't initialize rw lock for cache, quitting\n");
	}

	/* Setup size */
	mycache->cache_size = 0;
}

/* evict - 
 * Remove the oldest cached object from cache 
*/
void evict(void) 
{
	cache_object *oldest = mycache->head;
	cache_object *cur_object = mycache->head;

	read_lock();

	/* Find oldest */
	while(cur_object != NULL) 
	{

		/* Use <= since we insert at head and time() lacks enough resolution sometimes */
		if(cur_object->timestamp <= oldest->timestamp) 
		{
			oldest = cur_object;
		}

		cur_object = cur_object->next;
	}

	unlock();

	write_lock();

	/* Remove from linked list */
	removeFromList(oldest);

	/* Update cache size */
	mycache->cache_size -= oldest->size;

	/* Free */
	Free(oldest->uri);
	Free(oldest->response);
	Free(oldest);

	unlock();
}

/* updateTime - 
 * Update timestamp of object 
*/
void updateTime(cache_object *object) 
{
	write_lock();
	object->timestamp = time(NULL);
	unlock();
}

/* locateInCache - 
 * Find a cache object based on uri 
*/
struct cache_object *locateInCache(char *uri) 
{
	cache_object *cur_object = mycache->head;

	read_lock();

	while(cur_object != NULL) 
	{
		if(strcmp(uri, cur_object->uri) == 0) 
		{
			unlock();
			return cur_object;
		}
		cur_object = cur_object->next;
	}

	unlock();

	return NULL;
}


/* addToCache - 
 * Store a site in the cache 
*/
void addToCache(char *response, char *uri,  size_t size) 
{
	cache_object *cur;

	/* Include uri in size calculation */
	size_t total_size = size + strlen(uri) + 1;

	/* If there's not enough room, evict oldest */
	while(checkCacheSize(total_size) == 0) 
	{
	    evict();
    	}

	/* Insert in cache */
	write_lock();

	/* Setup cache object for this requested obj */
	cur = Calloc(1, sizeof(struct cache_object));
	(cur->uri) = Malloc(strlen(uri) + 1);
	strcpy(cur->uri, uri);
	(cur->response) = Malloc(size);
	memcpy(cur->response, response, size);
	(cur->timestamp) = time(NULL);
	(cur->size) = total_size;

	/* Insert into head of list */
	if(!(mycache->head)) 
	{
		(mycache->head) = cur;
	} 
	else 
	{
		(cur->next) = (mycache->head);
		((mycache->head)->prev) = cur;
		(mycache->head) = cur;
		(cur->prev) = NULL;
	}

	/* Update cache info */
	(mycache->cache_size) += total_size;

	unlock();
}

/* checkCacheSize - 
 *
*/
int checkCacheSize(size_t size) 
{
	read_lock();

	if((MAX_CACHE_SIZE - (mycache->cache_size)) < size) 
	{
		unlock();
		return 0;
	}

	unlock();
	return 1;
}

/* checkCache - 
 *
*/
int checkCache(char *uri) 
{
	if(locateInCache(uri)) 
	{
		return 1;
	} 
	else 
	{
		return 0;
	}
}

/* getFromCache - 
 *
*/
void getFromCache(int clientfd, char *uri) 
{
	char *response;
	size_t size;
	cache_object *cur = locateInCache(uri);

	/* Update timestamp for read */
	updateTime(cur);
    
	/* Locate in cache locks */
   	response = (cur->response);
   	size = (cur->size);

   	/* Lock here for reading */
   	read_lock();

	/* Deliver page to user */
	if(rio_writen(clientfd, response, size) < 0) 
	{
		unlock();
		cleanPtrs(NULL, NULL, NULL, NULL, NULL, NULL, uri, clientfd, -1);
		Pthread_exit(NULL);
	}
	
	unlock();
	return;
}

/* removeFromList - 
 *
*/
void removeFromList(cache_object *object) 
{
	if(((object->next) == NULL) && ((object->prev) == NULL)) 
	{
		(mycache->head) = NULL;
	} 
	else if(((object->next) == NULL) && ((object->prev) != NULL)) 
	{
		((object->prev)->next) = NULL;
	} 
	else if(((object->next) != NULL) && ((object->prev) == NULL)) 
	{
		((object->next)->prev) = NULL;
		(mycache->head) = (object->next);
	} 
	else 
	{
		((object->next)->prev) = (object->prev);
		((object->prev)->next) = (object->next);
	}
}


/* Locking and unlocking Functions */

/* write_lock - 
 *
*/
void write_lock() 
{
	if(pthread_rwlock_wrlock((pthread_rwlock_t *)&(mycache->lock)))
	{
		fprintf(stderr, "Couldn't write lock.\n");
		exit(-1);
	}
}

/* read_lock - 
 *
*/
void read_lock() 
{
	if(pthread_rwlock_rdlock((pthread_rwlock_t *)&(mycache->lock)))
	{
		fprintf(stderr, "Couldn't read lock.\n");
		exit(-1);
	}
}

/* unlock - 
 *
*/
void unlock() 
{
	if(pthread_rwlock_unlock((pthread_rwlock_t *)&(mycache->lock)))
	{
		fprintf(stderr, "Can't unlock.\n");
		exit(-1);
	}
}
