#include "csapp.h"  
void echo(int connfd);  
void command(void);  

int main(int argc, char **argv)  
{  
    int listenfd, connfd, port;  
    socklen_t clientlen = sizeof(struct sockaddr_in);  
    struct sockaddr_in clientaddr;  
    fd_set read_set, ready_set;  

    if (argc != 2)  
    {  
        fprintf(stderr, "usage: %s <port>\n", argv[0]);  
        exit(0);  
    }  

    port = atoi(argv[1]);  
    listenfd  = Open_listenfd(port);  

    FD_ZERO(&read_set);                    /* clear read set. */  
    FD_SET(STDIN_FILENO, &read_set);       /* add stdin to read set. */  
    FD_SET(listenfd, &read_set);           /* add listenfd to read set. */  
    //printf("hhhhhhhhhhhhhhhhhhhhhhh\n");  
    while (1)  
    {  
        ready_set = read_set;  
        //  printf("---------------------: %d\n", listenfd + 1);  
        Select(listenfd + 1, &ready_set, NULL, NULL, NULL);  
        //  printf("11111111111111111111111111\n");  
        if (FD_ISSET(STDIN_FILENO, &ready_set))  
        {  
            command();                 /* read command line from stdin. */  
        }  
        //  printf("------------------------\n");  
        if (FD_ISSET(listenfd, &ready_set))  
        {  
            connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  
            echo(connfd);             /* echo client input until EOF. */  
            Close(connfd);  
        }  
    }  
}  


void command(void)  
{  
    char buf[MAXLINE];  
    if (!Fgets(buf, MAXLINE, stdin))  
        exit(0);   /* EOF */  

    printf("%s\n", buf);    /* Process the input command. */  
}  

void echo(int connfd)  
{  
    size_t n;  
    char buf[MAXLINE];  
    rio_t rio;  

    Rio_readinitb(&rio, connfd);  
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)  
    {  
        printf("server received %lu by66tes \n", n);  
        //printf("%s\n", buf);  
        Rio_writen(connfd, buf, n);  
    }  
}
