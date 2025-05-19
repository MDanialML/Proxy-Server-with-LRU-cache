#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>


//macros and global variables
#define MAX_CLIENTS 10  //macimum clients(threads) that can be active concurrently
#define MAX_BYTES 4096  //maximum number of bytes read/written per operation(buffer size)
#define MAX_ELEMENT_SIZE 10*(1<<20)   //maximum size for an individual cache element
#define MAX_SIZE 100*(1<<20)    //maximum total cache size(in bytes)
typedef struct cache_element cache_element;
typedef struct ParsedRequest ParsedRequest;



//Global variables
int port_number = 8080; //port on which the proxy server listens
int proxy_socketId; //Then server's listening socket descriptor
pthread_t tid[MAX_CLIENTS]; //array of thread IDs


//Synchronization primitives
sem_t semaphore;    //A semaphore to limit the number of active clients
pthread_mutex_t lock; //A mutex used to ensure thread-safe access to the cache

//Cache Variables
cache_element* head; //points to head of linked list that implements cache
int cache_size;     //Track the total size(in bytes) of the cached data


//Cache Element Structure
struct cache_element{
    char* data;     //pointer to the cached response data
    int len;        //Length (in bytes) of the data
    char* url;      //The URL (or key) associated with this cached data
    time_t lru_time_track;  //Timestamp used to track the "last used" time for LRU eviction
    cache_element* next;    //Pointer to the next element in the linked list
};

//required function declaration
cache_element* find(char* url);
int add_cache_element(char* data, int size, char* url);
void remove_cache_element(); 


//connect to remote server
int connectedRemoteServer(char* host_addr, int port_num){

    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket<0){
        printf("Error in creating your socket\n");
        return -1;
    }
    //resolve host name
    struct hostent* host = gethostbyname(host_addr);
    if(host==NULL){
        fprintf(stderr, "No such host exist\n");
        return -1;
    }

    //setup server address structure
    struct sockaddr_in server_addr;
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    bcopy((char *)&host->h_addr_list[0], (char *)&server_addr.sin_addr.s_addr, host->h_length);

    //connect to the remote server
    if(connect(remoteSocket, (struct sockaddr *)&server_addr, (size_t)sizeof(server_addr))<0){
            fprintf(stderr, "Error in connecting \n");
            return -1;
    }

    return remoteSocket;

}

//handle client request
int handle_request(int clientSocketId, ParsedRequest *request, char* tempReq){
    
    //construct a get request
    char *buff = (char *)malloc(sizeof(char) *MAX_BYTES);
    strcpy(buff, "GET ");
    strcat(buff, request->path);
    strcat(buff, " ");
    strcat(buff, request->version);
    strcat(buff, "\r\n");
    size_t len = strlen(buff);

    if(ParsedHeader_set(request, "Connection", "close")<0){
        printf("Set header key is not working");
    }

    if(ParsedHeader_get (request, "Host") == NULL){
        if(ParsedHeader_set(request, "Host", request->host) < 0){
            printf("Set Host header key is not working ");
        }
    }

    if(ParsedRequest_unparse_headers(request, buff+len, (size_t)MAX_BYTES-len)<0)
    {
        printf("Unparse Failed\n");
    }

    int server_port = 80;
    if(request->port != NULL){
        server_port = atoi(request->port);
    }
    int remoteSocketId = connectedRemoteServer(request->host, server_port);
    
    if(remoteSocketId < 0)
    {
        return -1;
    }

    //Send the constructed Get request to the remote server
    int bytes_send = send(remoteSocketId, buff, strlen(buff), 0);
    bzero(buff, MAX_BYTES);

    //Recieve response from remote server
    bytes_send = recv(remoteSocketId, buff, MAX_BYTES-1, 0);
    char * temp_buffer = (char *) malloc(sizeof(char)*MAX_BYTES);
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;
    
    //loop to send the response to the client and copy data for caching
    while (bytes_send > 0)
    {
        bytes_send = send(clientSocketId, buff,  bytes_send, 0);
        for(int i = 0; i <bytes_send/sizeof(char); i++)
        {
            temp_buffer[temp_buffer_index] = buff[i];
            temp_buffer_index++;
        }
        
        //adjust the size for the next chunk if needed
        temp_buffer_size = MAX_BYTES;
        temp_buffer = (char *) realloc(temp_buffer, temp_buffer_size);
        if(bytes_send < 0){
            perror("Error in sending data to the client \n");
            break;
            }
        bzero(buff, MAX_BYTES);
        bytes_send = recv(remoteSocketId, buff, MAX_BYTES-1, 0);
    }
    temp_buffer[temp_buffer_index] = '\0';
    free(buff);

    //cache the response
    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    free(temp_buffer);
    close(remoteSocketId);

    return 0;
}

int checkHTTPversion(char *msg)
{
    int version = -1;
    if(strcmp(msg, "HTTP/1.1") == 0)
    {
        version = 1;
    }
    else if(strcmp(msg, "HTTP/1.0") == 0)
    {
        version = 1;
    }
    else
        version = -1;
    return version;
}

int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}



void *thread_fn(void* socketNew)
{   
    //wait on the semaphore to limit cucurrent client handling
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("semaphore value is: %d\n", p);
    
    //extract the socket descriptor from the passed argument
    int *t = (int*) socketNew;
    int socket = *t;

    //allocate a buffer and recieve the client's HTTP request
    int bytes_send_client, len;
    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES);
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);

    //Continue recieving until the end-of-headers marker ("\r\n\r\n") found
    while(bytes_send_client > 0){
        len = strlen(buffer);
        if(strstr(buffer, "\r\n\r\n") == NULL){
            bytes_send_client = recv(socket, buffer+len, MAX_BYTES-len, 0);
        }else{
            break;
        }
    }

    //Duplicate the recieved request into tempReq
    len = strlen(buffer);
    char *tempReq = (char *) malloc (len+1);
    if(tempReq == NULL){
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    strcpy(tempReq, buffer);
    // for(int i = 0; i < strlen(buffer); i++){
        // temReq[i] = buffer[i];
    // }
    //check whether the data is in cache and then send to client
    struct cache_element* temp = find(tempReq);
    
    if(temp!= NULL){
        //Cache hit: Send the cached response to the client
        int size = temp->len;
        int pos = 0;
        char response[MAX_BYTES];
        while(pos<size){
            bzero(response, MAX_BYTES);
            for(int i = 0; i < MAX_BYTES && pos < size; i++)
            {
                response[i] = temp -> data[pos];
                pos++;
            }
            // send(socket, response, MAX_BYTES, 0);
            int bytes_to_send = (size-pos) > MAX_BYTES ? MAX_BYTES : (size - pos);
            send(socket, response, bytes_to_send, 0);
        }
        printf("Data retrieved from cache\n");
        printf("%s\n\n", response);
    }
    
    //Cache miss: Parse the request and handle it by contacting the remote server
    else if(bytes_send_client > 0){
        len = strlen(buffer);
        ParsedRequest *request = ParsedRequest_create();

        if(ParsedRequest_parse(request, buffer, len) < 0){
            printf("parsing failed \n");
        }else{
            bzero(buffer, MAX_BYTES);

            if(!strcmp(request->method, "GET")){

                if(request->host && request->path && checkHTTPversion(request->version) == 1){
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if(bytes_send_client == -1)
                    {
                        sendErrorMessage(socket, 500);
                    }
                }else{
                    sendErrorMessage(socket, 405);
                    // printf("This code doesn't support any mehtod apart from Get \n");
                }
            }
        }
        ParsedRequest_destroy(request);
    }
    else if(bytes_send_client == 0)
    {
        printf("Client is disconneted \n");
    }

    //Cleanup: shutdown and close client socket, free memory
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);
    sem_getvalue(&semaphore, &p);
    printf("Semaphore post value is %d \n", p);
    free(tempReq);
    return NULL;
}


int main(int argc, char* argv[])
{
    //Command-Line argument parsing initializing server port provided by user.
     if(argc == 2)
    {
        port_number = atoi(argv[1]);
    }
    else
    {
        printf("Too few arguments \n");
        exit(1);
    }
    printf("Starting Proxy server at port: %d\n", port_number);

    
    //struct to hold client and server address detail
    struct sockaddr_in server_addr, client_addr; 

    //Semaphore initialization 
    sem_init(&semaphore, 0, MAX_CLIENTS);

    //Mutex Initialization, Ensure synchronized access to chache shared resource
    pthread_mutex_init(&lock, NULL);
    
    //create socket for proxy server
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0); 
    
    //check whether socket is created successfully or not
    if(proxy_socketId < 0)
    {
        perror("Failed to create a socket \n");
        exit(1);
    }

    //Setting the socket options i,e socket reuse 
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0){
        perror("setSocketOpt failed\n");
    }else{
        perror("Socket reuse successful\n");
    }

    //initialize/clear the structure because it initially holds garbage values
    bzero ((char*)&server_addr,sizeof(server_addr));

    //Initializing server_addr structure for binding
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    //Bind socket to port
    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Socket is not available");
        exit(1);
    }
    printf("Binding on port %d\n", port_number);

    //Listen client requests
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if(listen_status < 0)
    {
        perror("Error in listening");
        exit(1);
    }

    //handel client connections
    int i = 0; // Index to track client connections
    int Connected_socketId[MAX_CLIENTS]; //array to store connected clients socket IDs
    socklen_t client_len; //Length of clients address structure 
    int client_sockId; // Socket ID for a new client

    while (1)
    {   //clear the client address structure
        bzero((char *)&client_addr, sizeof(client_addr));

        // Initialize client_len with the size of client_addr structure
        client_len = sizeof(client_addr);

        //accept a new client connection
        client_sockId = accept(proxy_socketId, (struct sockaddr*)&client_addr,
                    (socklen_t*)&client_len);
        
        //Handle connection errors
        if(client_sockId < 0)
        {
            printf("Not able to connect");
            exit(1);
        }else{

            //Save the socket ID in the array
            Connected_socketId[i] = client_sockId;
        }
        //Extract and display the client's IP address and port number
        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str [INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, str, INET6_ADDRSTRLEN);
        printf("Client is connected with port number %d and ip address is %s \n", ntohs(client_addr.sin_port), str);

        //Create a new thread to handle the clients request
        pthread_create (&tid[i], NULL, thread_fn, (void*) &Connected_socketId[i]);
        i++; //Increment the index to track the next connection
    }

    //close the proxy socket after exiting the loop
    close(proxy_socketId);
    return 0;
    
}


cache_element *find(char* url){
    cache_element *site = NULL;
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove cache Lock acquired %d\n", temp_lock_val);
    if(head != NULL){
        site = head;
        while (site != NULL)
        {
            if(!strcmp(site->url, url)){
                printf("LRU time track before: %ld", site-> lru_time_track);
                printf("\n url found \n");
                site -> lru_time_track = time(NULL);
                printf("LRU time track after %ld", site->lru_time_track);
                break;
            }
            site = site->next;
        }
    }
    else{
        printf("url not found \n");
    }
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Lock is unlocked \n");
    return site;
}



void remove_cache_element()
{
    cache_element *p;
    cache_element *q;
    cache_element *temp;

    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("lock is acquired \n");
    if(head != NULL)
    {
        for(q=head, p=head, temp = head; q->next != NULL; q = q->next)
        {
            if(((q->next)->lru_time_track) < temp->lru_time_track){
                temp = q->next;
                p = q;
            }
        }
        if (temp == head){
            head = head->next;
        }else{
            p->next = temp->next;
        }

        //if the cache is not empty searches for the node which has the least lru_time_track
        cache_size = cache_size - (temp->len) - sizeof(cache_element) - strlen(temp->url)-1;
        free(temp->data);
        free(temp->url);
        free(temp);
    }

    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove cache lock\n");


}

int add_cache_element (char *data, int size, char *url){
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add cache Lock Acquired %d\n", temp_lock_val);
    int element_size = size+1+strlen(url)+sizeof(cache_element);
    if (element_size < MAX_ELEMENT_SIZE)
    {
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add cache lock is unlocked \n");
        return 0;
    }
    else{
        while (cache_size + element_size > MAX_SIZE)
        {
            remove_cache_element();
        }
        cache_element *element = ((cache_element*)malloc(sizeof(cache_element)));
        element->data = (char *)malloc(size+1);
        strcpy(element->data, data);
        element->url = (char *) malloc(1+ (strlen(url)*sizeof(char)));
        strcpy(element->url, url);
        element->lru_time_track = time(NULL);
        element->next = head;
        element->len = size;
        head = element;
        cache_size += element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add cache lock is unllocked \n");
        return 1;
    }
    return 0;
    
}