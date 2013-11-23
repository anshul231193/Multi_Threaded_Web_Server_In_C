#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <pthread.h>
#include <dirent.h>

/*
 * DIRECTORY STRUCTURE
 */
char dir_root[200], custom_dir[200], tilde_root[200], tilde_user[20];

/*
 * QUEUE NODE STRUCTURE
 */
typedef struct node{
	int acceptfd;
	char file_name[20];
	char file_path[200];
	char request_type[5];
	int file_size;	
	char client_ip[20];
	char content_type[15];
	char arrival_time[30];
	char current_dir[200];
	struct node *next;
	struct node *previous;
} Node;

/*
 * WAITING QUEUE AND READY QUEUE DECLARATION
 */
typedef struct queue {
	Node *front, *rear;
} Queue;

Queue waiting_queue = { NULL, NULL }, ready_queue = { NULL, NULL };

/*
 * FUNCTION DECLARATION 
 */
void usage();
void parse_input();
void listener_routine(void *sock_server);
void scheduler_routine();
void worker_routine();
void parse_request(char request[], int acceptfd, char client_ip[]);
Node *create_queue_node(int acceptfd, char request_type[], char file_name[], char client_ip[], int file_size, char file_path[], char content_type[], char current_dir[]);
int insert_into_queue(Queue *queue, Node *new_node);
void display_queue(Queue *queue, char *queue_type);
void print_node(Node *node);
Node *dequeue_using_SJF(Queue *queue);
Node *dequeue_using_FCFS(Queue *queue);

char *get_http_status(Node *node, char http_status[]);
void get_current_time(char current_timestamp[]);
void get_last_modified_time_of_file(char last_modified[], Node *removed_node);


void get_request_type(char request[], char request_type[]);
void get_file_path(char file_path[], char file_name[], char current_dir[]);
void get_content_type(char content_type[], char file_name[]);
long int get_file_size(char file_path[]);
void get_file_name(char request[], char file_name[]);
void append_to_log_file(char client_ip[], char arrival_time[], char current_timestamp[], char first_line_of_request[], char *http_status, char char_file_size[]);
void add_directory_content(char *buffer, Node *node);

/* 
 * GLOBAL VARIABLES
 */
int port_number = 8080, THREADNUM = 4, SLEEP_TIME = 60;
int help_flag = 0, dir_flag = 0;
char *host = NULL, *port = NULL, *dir, log_file_name[10];
extern char *optarg;
extern int optopt;
int use_SJF = 0, create_log = 0, tilde_present = 0, custom_root_dir = 0, file_not_found = 0, debug = 0;


/* 
 * MUTEX AND CONDITION VARIABLE DECLARATION
 */
pthread_mutex_t waiting_queue_mutex;
pthread_mutex_t ready_queue_mutex;
pthread_cond_t waiting_queue_empty, ready_queue_empty;

/*
 * MAIN METHOD BEGINS
 */
int main(int argc, char *argv[]){
	int sock_server, i;
	struct sockaddr_in server;
	pthread_t listener, scheduler, worker[10];

	/* Initialize mutex and condition variable objects */
	pthread_mutex_init(&waiting_queue_mutex, NULL);
	pthread_mutex_init(&ready_queue_mutex, NULL);
	pthread_cond_init(&waiting_queue_empty, NULL);
	pthread_cond_init(&ready_queue_empty, NULL);
	
	/* Parse the attributes provided to the program */
	parse_input(argc, argv);
	
	if (help_flag == 1){
		usage();
		exit(1);
	}

	if (dir_flag == 1){
		if (chdir(dir) < 0){
			perror("\nDirectory does not exist !\n");
			exit(1);
		}
	}

	/* setup server */ 
	server.sin_family = AF_INET;
	server.sin_addr.s_addr	= INADDR_ANY;
	server.sin_port = htons(port_number);
 	printf("Listening on port : %d\n", port_number);	

	/* create socket on local host */
	sock_server = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_server < 0){
		perror("Error creating socket for server\n");
		exit(1);
	}
	//printf("Socket for server created successfully. It is : %d\n", sock_server);

	/* call bind */
	if ( bind(sock_server, (struct sockaddr *) &server, sizeof(server)) < 0 ){
		perror("Bind for server failed.\n");
		exit(1);
	}

	/* create listener thread */
	if( pthread_create(&listener, NULL, (void *) &listener_routine, (void *) &sock_server) != 0){
		perror("Error creating the listener thread\n");
	}

	/* create scheduler thread */
	sleep(SLEEP_TIME);
	if( pthread_create(&scheduler, NULL, (void *) &scheduler_routine, NULL) != 0){
		perror("Error creating the scheduler thread\n");
	}

	/* #TODO create worker threads */
	sleep(5);
	for (i = 0; i < THREADNUM ; i++){
		if( pthread_create(&worker[i], NULL, (void *) &worker_routine, NULL) != 0){
			perror("Error creating the worker thread\n");
		}
	}
	
	/* Join listener and scheduler with the main thread */
	pthread_join(listener, NULL);
	pthread_join(scheduler, NULL);
	for (i = 0; i < THREADNUM; i++){
		pthread_join(worker[i], NULL);
	}
	
	pthread_mutex_destroy(&waiting_queue_mutex);
	pthread_mutex_destroy(&ready_queue_mutex);
	pthread_cond_destroy(&waiting_queue_empty);
	pthread_exit(NULL);
}

/* 
 * PARSE THE INPUT FOR MAIN METHOD 
 */

void parse_input(int argc, char *argv)
{
	char ch;

	while ((ch = getopt(argc, argv, "dhl:p:r:t:n:s:")) != -1)
	{
		switch(ch) 
		{
			case 'd':
				// Enter debuging mode creating only one worker thread
				THREADNUM = 1;
				debug = 1;
				printf("In debugging mode\n");
				break;
			case 'h':
				// Print usage summary with all options and exit
				help_flag = 1;
				break;
			case 'l':
				// Log all requests for a given file
				create_log = 1;
				strcpy(log_file_name, optarg);
				break;
			case 'p':
				port_number = atoi(optarg);
				break;
			case 'r':
				// Set the root directory for http server
				custom_root_dir = 1;
				strcpy(custom_dir, optarg);
				printf("Custom dir is : %s\n", custom_dir);
				break;
			case 't':
				// Set the queuing time to time seconds
				SLEEP_TIME = atoi(optarg);
				break;
			case 'n':
				// Set the number of threads waiting ready in execution thread pool. Default = 4
				THREADNUM = atoi(optarg);
				break;
			case 's':
				// Set the scheduling policy. Default : FCFS
				if( (strcmp(optarg, "SJF") == 0) || (strcmp(optarg, "sjf") == 0) ){
					use_SJF = 1;
					printf("Scheduling Policy chosen is : SJF\n");
				}
				break;
			case '?':
				if (optopt == 'p' || optopt == 'r' || optopt == 't' || optopt == 'n' || optopt == 's')
					fprintf(stderr, "Option - %c need an arguement\n", optopt);
				else
					fprintf(stderr, "Unknown option - %c", optopt);
				break;
			default:
				usage();
		}
	}
	if (debug == 0)
		daemon(1, 0);
	if (use_SJF == 0)
		printf("Scheduling Policy chosen is : FCFS\n\n");
}

/*
 * LISTENER ROUTINE BEGINS
 */
void listener_routine(void *sock_server){
	/* create a listener which continuously listens for the incoming requests */
	int sockfd = *((int *)sock_server), return_value, acceptfd;
	struct sockaddr_in client;
	socklen_t client_len;
	char buffer[512];
	pthread_t worker[10];
	char client_ip[20];
		
	/* listen for incoming requests */ 
	listen(sockfd, 5);

	/* keep listening */
	while(1)
	{	
		/* accept the incoming connection */
		acceptfd = accept(sockfd, (struct sockaddr *) &client, &client_len);
		
		/* get the client IP */
		strcpy(client_ip, (char *)inet_ntoa(client.sin_addr)); 

		if (acceptfd == -1){
			perror("Error in accepting the client\n");
		}
		
		if (acceptfd > 0){
			/* recieve the request */
			return_value = recv(acceptfd, &buffer, sizeof(buffer), 0);

			if(return_value < 0)
				perror("Error occurred in listener:recv function\n");
			
			if(return_value == 0)
				printf("Ending Connection !\n");
			
			if(return_value > 0){
				/* parse the incoming request */
				pthread_mutex_lock(&waiting_queue_mutex);
				printf("Listener acquired the lock\n");
				parse_request(buffer, acceptfd, client_ip);
				pthread_mutex_unlock(&waiting_queue_mutex);
				printf("Listener released the lock\n");
			}
		}
	}
}

void parse_request(char request[], int acceptfd, char client_ip[]){
	int i = 0, j = 0, signal = 0;
	char file_name[20], request_type[5], file_path[200], content_type[15], current_dir[200];
	long int file_size = 0;
	FILE *fp;
	Node *new_node;

	/* TODO Write a separate method to get request type and request path : present code looks CRAPPY :P yaaakksss ! */
	/* Get the request type and the file path */
	get_request_type(request, request_type);
	get_file_name(request, file_name);
	if (strcmp(file_name, "favicon.ico") != 0){
		get_content_type(content_type, file_name);
		get_file_path(file_path, file_name, current_dir);
		if(strcmp(request_type, "HEAD") == 0)
			file_size = 0;
		else
			file_size = get_file_size(file_path);
		new_node = (Node *)malloc(sizeof(Node));
		/* Take the lock on waiting queue and Create and insert node in the waiting queue */
		new_node = create_queue_node(acceptfd, request_type, file_name, client_ip, file_size, file_path, content_type, current_dir);
		get_current_time(new_node -> arrival_time);
		signal = insert_into_queue(&waiting_queue, new_node);
		if(signal){
			printf("Signaling the scheduler\n");
			printf("Listener(): released the lock\n");
			pthread_cond_signal(&waiting_queue_empty);
		}	
		display_queue(&waiting_queue, "Waiting Queue");
	}
}

long int get_file_size(char file_path[]){
	FILE *fp;
	int file_size;

	fp = fopen(file_path, "r+");
	if (fp == NULL){
		perror("Error in file opening");
		return 0;
	}
	else{
		fseek(fp, 0L, SEEK_END);
		file_size = ftell(fp);	
		fseek(fp, 0L, SEEK_SET);
		fclose(fp);
		return file_size;
	}
}

void get_file_path(char file_path[], char file_name[], char current_dir[]){
	memset(file_path, 0, sizeof(file_path));
	memset(dir_root, 0, sizeof(dir_root));
	memset(current_dir, 0, sizeof(current_dir));
	
	/* Overwrite everything if tilde is present */
	if (tilde_present){
		memset(tilde_root, 0, sizeof(tilde_root));
		strcat(tilde_root, "/home/");
		strcat(tilde_root, tilde_user);
		strcat(tilde_root, "/myhttpd/");
		strcpy(current_dir, tilde_root);
		strcat(tilde_root, file_name);
		strcpy(file_path, tilde_root);
		tilde_present = 0;
	}
	/* see for -r option */
	else if (custom_root_dir){
		strcat(dir_root, custom_dir);
		custom_dir[strlen(custom_dir) - 1];
		if ( custom_dir[strlen(custom_dir) - 1] != '/')	
			strcat(dir_root, "/");
		strcpy(current_dir, dir_root);
		strcat(dir_root, file_name);
		strcpy(file_path, dir_root);
	}
	/* use the file name as the file path*/
	else{
		strcpy(current_dir, "./");
		strcat(dir_root, file_name);
		strcpy(file_path, dir_root);
	}
	printf("New File path : %s\n\n", file_path);
}

void get_content_type(char content_type[], char file_name[]){
	int i = 0;
	char *file_extention;

	while(file_name[i] != '.'){ i++; }
	i++;
	file_extention = &file_name[i];
	if ( (strcmp(file_extention, "txt") == 0) || (strcmp(file_extention, "html") == 0) ){
		strcpy(content_type, "text/html");
	}
	
	if ( (strcmp(file_extention, "gif") == 0) || (strcmp(file_extention, "jpg") == 0) || (strcmp(file_extention, "jpeg") == 0) ){
		strcpy(content_type, "image/");
		strcat(content_type, file_extention);
	}
}

void get_file_name(char request[], char file_name[]){
	int i = 0, j = 0;
	char *ptr;
	
	while(request[i] != ' '){ i++; }
	i++;
	while(request[i] != ' '){
		file_name[j] = request[i];
		i++; j++;
	}
	file_name[j] = '\0';

	/* remove the first '/' character prepended with the file name */
	i = 0; j = 1;
	while(i < strlen(file_name)){
		file_name[i] = file_name[j];
		i++; j++;
	}

	/* test for presence of '~' in the file_name */
	i = 0;
	if( (ptr = strchr(file_name, '~')) != NULL ){
		ptr++;
		while(*ptr != '/'){
			tilde_user[i] = *ptr;
			ptr++; i++;
		}
		tilde_user[i] = '\0';
		ptr++;	
		strcpy(file_name, ptr);
		tilde_present = 1;
	}
}

void get_request_type(char request[], char request_type[]){
	int i = 0;

	while(request[i] != ' '){
		request_type[i] = request[i];
		i++;
	}
	request_type[i] = '\0';
}

/* 
 * INSERT NODE INTO THE SPECIFIED QUEUE 
 */
int insert_into_queue(Queue *queue, Node *new_node){
	int signal = 0;

	if (queue -> front == NULL && queue -> rear == NULL){   /* If queue is empty */
		queue -> front = new_node;
		queue -> rear = new_node;
		signal = 1;
	}
	else{
		new_node -> next = queue -> front;
		queue -> front -> previous = new_node;
		queue -> front = new_node;
	}
	return signal;
}

/* 
 * SCHEDULER ROUTINE BEGINS 
 */
void scheduler_routine(){
	Queue *queue;
	Node *removed_node;
	int signal = 0;

	queue = &waiting_queue;
	printf("Inside scheduler\n");
	while(1){
		pthread_mutex_lock(&waiting_queue_mutex);
		printf("Scheduler(): acquired the lock\n");
		if (queue -> front == NULL && queue -> rear == NULL){
			printf("scheduler(): Nothing to schedule => WAIT !\n");
			pthread_cond_wait(&waiting_queue_empty, &waiting_queue_mutex);
		}
		else{
			removed_node = (Node *)malloc(sizeof(Node));
			
			/* Take the lock on waiting queue and remove the item from it */	
			if(use_SJF){
				removed_node = dequeue_using_SJF(&waiting_queue);
			}
			else{
				removed_node = dequeue_using_FCFS(&waiting_queue);
			}
			display_queue(&waiting_queue, "Waiting Queue");

			/* Take the lock on ready queue and insert the item into it */
			pthread_mutex_lock(&ready_queue_mutex);
			printf("Scheduler(): Ready queue lock acquired\n");
			signal = insert_into_queue(&ready_queue, removed_node);
			display_queue(&ready_queue, "Ready Queue");
			if(signal){
				printf("Scheduler(): Signaling the workers\n");
				pthread_cond_signal(&ready_queue_empty);
			}
			pthread_mutex_unlock(&ready_queue_mutex);
			printf("Scheduler(): Ready queue lock released\n");
		}
		pthread_mutex_unlock(&waiting_queue_mutex);
		printf("Scheduler(): released the lock\n");
	}
}

/*
 * WORKER ROUTINE
 */
void worker_routine(){
	Queue *queue;
	Node *removed_node;
	//unsigned char buffer[16385];
	unsigned char *buffer, header[500], *fof_buffer;
	char *temp, http_status[20], current_timestamp[30], last_modified[30], char_file_size[80], ch, first_line_of_request[50];
	size_t file_length;
	FILE *fp;
	long int response_size;
	
	queue = &ready_queue;
	while(1){
		pthread_mutex_lock(&ready_queue_mutex);
		printf("Worker(): acquired the lock\n");

		/* If ready queue is empty : wait */
		if(queue -> front == NULL && queue -> rear == NULL){
			printf("Worker(): Nothing to serve => WAIT !\n");
			pthread_cond_wait(&ready_queue_empty, &ready_queue_mutex);
		}
		else{
			/* Dequeue the Ready queue */
			removed_node = dequeue_using_FCFS(&ready_queue);
			printf("Here Removed Node is : \n");
			print_node(removed_node);
			display_queue(&ready_queue, "Ready Queue");

			/* create header */
			memset(header, 0, sizeof(header));
			
			/* 1st line */
			strcat(header, "HTTP/1.1 ");
			get_http_status(removed_node, http_status);
			strcat(header, http_status);
			strcat(header, "\n");
			
			/* 2nd line */
			get_current_time(current_timestamp);
			strcat(header, "Date: ");
			strcat(header, current_timestamp);
			strcat(header, "\n");

			/* 3rd line */
			strcat(header, "Server: myhttpd-ketan 1.0\n");

			/* 4th line */
			get_last_modified_time_of_file(last_modified, removed_node);
			strcat(header, "Last-Modified: ");
			strcat(header, last_modified);
			strcat(header, "\n");

			/* 5th line */
			strcat(header, "Content-Type: ");
			strcat(header, removed_node -> content_type);
			strcat(header, "\n");

			if (file_not_found){
				/* 404 File NOT FOUND */
				/* Append the directory structure in the buffer and send */
				buffer = (unsigned char *)malloc(2000*sizeof(char));
				fof_buffer = (unsigned char *)malloc(1000*sizeof(char));
				memset(fof_buffer, 0, sizeof(fof_buffer));
				printf("Error in opening the file !\n");
				strcat(fof_buffer, "<html><body>");
				strcat(fof_buffer, "<h2>404 : File not found !</h2><h4>Contnet in the current directory is : </h4>");
				add_directory_content(fof_buffer, removed_node);
			}
			else
				buffer = (unsigned char *)malloc(((strlen(header)) + removed_node -> file_size)*sizeof(char));
			memset(buffer, 0, sizeof(buffer));
			
			/* 5th line */ 
			strcat(header, "Content-Length: ");
			if(file_not_found){
				printf("Buffer Length is : %d\n\n", strlen(fof_buffer));
				sprintf(char_file_size, "%d", strlen(fof_buffer));
			}
			else
				sprintf(char_file_size, "%d", removed_node -> file_size);
			strcat(header, char_file_size);
			strcat(header, "\n\n"); /* extra blank line required */
				
			/* Copy the file in buffer */
			temp = (char *)malloc(removed_node -> file_size + 1);
			strcat(buffer, header);
			fp = fopen(removed_node -> file_path, "r");
			if (fp != NULL){
				fread(temp, removed_node -> file_size, 1, fp);
				fclose(fp);
				temp[removed_node -> file_size] = 0;
				strcat(buffer, temp);
			}
			if(file_not_found){
				strcat(buffer, fof_buffer);
				file_not_found = 0;
			}

			printf("Buffer is : \n%s", buffer);

			/* appende to log file */
			if (create_log){
				memset(first_line_of_request, 0, sizeof(first_line_of_request));
				strcat(first_line_of_request, removed_node -> request_type);
				strcat(first_line_of_request, " ");
				strcat(first_line_of_request, removed_node -> file_name);
				strcat(first_line_of_request, " HTTP/1.0");
				append_to_log_file(removed_node -> client_ip, removed_node -> arrival_time, current_timestamp, first_line_of_request, http_status, char_file_size);
			}
			
			/* send the buffer to client */
			send(removed_node -> acceptfd, buffer, strlen(buffer), 0);
		}
		pthread_mutex_unlock(&ready_queue_mutex);
		printf("Worker(): released the lock\n");
	}
}

void add_directory_content(char *buffer, Node *node){
	char filename[512];
	struct dirent **namelist;
	int n = scandir(node -> current_dir, &namelist, 0, alphasort);
	int i;

	for ( i = 0; i < n; i++ )
	{ 
		char *file_name = namelist[i]->d_name;

		/*strcpy(filename, "./");
		strcat(filename, "/");
		strcat(filename, file_name);*/
		strcat(buffer, "<p>");
		strcat(buffer, file_name); 
		strcat(buffer, "</p>");
	}
	strcat(buffer, "</body></html>");
}

void append_to_log_file(char client_ip[], char arrival_time[], char current_timestamp[], char first_line_of_request[], char *http_status, char char_file_size[]){
	char buffer[500];
	FILE *fp;

	memset(buffer, 0, sizeof(buffer));
	strcat(buffer, client_ip);
	strcat(buffer, " - [");
	strcat(buffer, arrival_time);
	strcat(buffer, "] [");
	strcat(buffer, current_timestamp);
	strcat(buffer, "] '");
	strcat(buffer, first_line_of_request);
	strcat(buffer, "' ");
	strcat(buffer, http_status);
	strcat(buffer, " ");
	strcat(buffer, char_file_size);
	printf("Log Content is : \n%s\n", buffer);

	fp = fopen(log_file_name, "a");
 	fprintf(fp, "%s\n", buffer);	
	fclose(fp);
}

void get_last_modified_time_of_file(char last_modified[], Node *node){
	struct stat file_info;
	char *p;
	int i = 0;

	/* function ctime appends extra /n character to the returned time hence written following code to get rid of it */
	if (!stat(node -> file_path, &file_info)) {
		p = ctime(&file_info.st_mtime);
		while(p[i] != '\0'){
			if(p[i] == '\n'){ 
				p[i] = '\0';
			}
			last_modified[i] = p[i];
			i++;
		}
	} 
	else {
		printf("Cannot display the time.\n");
	}
}

void get_current_time(char current_timestamp[]){
	time_t current_time;
	int i =0;
	char *p;

	/* function ctime appends extra /n character to the returned time hence written following code to get rid of it */
	time(&current_time);
	p = ctime(&current_time);
	while(p[i] != '\0'){
		if(p[i] == '\n'){ 
			p[i] = '\0';
		}
		current_timestamp[i] = p[i];
		i++;
	}
}

char *get_http_status(Node *node, char http_status[]){
	struct stat x;

	memset(http_status, 0 , sizeof(http_status));
	if ( (stat(node -> file_path, &x)) != 0){
		printf("fopen failed !\n");
		strcpy(http_status, "404 NOT FOUND");
		file_not_found = 1;
	}
	else{
		strcpy(http_status, "200 OK");
	}
	return http_status;
}

/* 
 * DEQUEUE USING FCFS ALGORITHM 
 */
Node *dequeue_using_FCFS(Queue *queue){
	Node *temp = NULL;

	if(queue -> front == NULL && queue -> rear == NULL){
		printf("\n\nThere are no elements present for dequing ! Something is wrong !\n\n");
	}

	if( (queue -> front != NULL) && (queue -> rear != NULL) && (queue -> front == queue -> rear) ){ /* If only one element exists */
		temp = queue -> front;
		queue -> front = NULL;
		queue -> rear = NULL;
	}
	else{
		temp = queue -> rear;
		queue -> rear = queue -> rear -> previous; 	
		queue -> rear -> next = NULL;
	}
	return temp;
}

/* 
 * DEQUE USING SJF ALGORITHM
 */
Node *dequeue_using_SJF(Queue *queue){
	Node *temp = NULL;
	Node *iterator;
			
	if( (queue -> front != NULL) && (queue -> rear != NULL) && (queue -> front == queue -> rear) ){ /* If only one element exists */
		temp = queue -> front;
		queue -> front = NULL;
		queue -> rear = NULL;
	}
	else{
		iterator = queue -> front;
		temp = iterator;
		while (iterator != NULL){
			if (temp -> file_size > iterator -> file_size){
				temp = iterator;
			}
			iterator = iterator -> next;
		}
		
		/* change queue front and back */
		if(temp == queue -> front){
			queue -> front = temp -> next;
		}
		
		if(temp == queue -> rear){
			queue -> rear = temp -> previous;
		}

		/* change links to maintain list integrity */
		if(temp -> previous != NULL)
			temp -> previous -> next = temp -> next;
		if(temp -> next != NULL)
			temp -> next -> previous = temp -> previous;
		temp -> previous = NULL;
		temp -> next = NULL;
	}
	return temp;
}

/* 
 * CREATE A NODE FOR QUEUE 
 */
Node *create_queue_node(int acceptfd, char request_type[], char file_name[], char client_ip[], int file_size, char file_path[], char content_type[], char current_dir[]){
	Node *new_node = (Node *)malloc(sizeof(Node));

	strcpy(new_node -> file_name, file_name);
	strcpy(new_node -> request_type, request_type);
	new_node -> acceptfd = acceptfd;
	strcpy(new_node -> client_ip, client_ip);
	new_node -> file_size = file_size;
	strcpy(new_node -> file_path, file_path);
	strcpy(new_node -> content_type, content_type);
	strcpy(new_node -> current_dir, current_dir);
	new_node -> next = NULL;
	new_node -> previous = NULL;
	return new_node;
}

/* 
 *  DISPLAY ALL THE ELEMENTS IN QUEUE
 */

void display_queue(Queue *queue, char *queue_type){
	Node *iterator;

	iterator = queue -> front;
	printf("%s\n", queue_type);
	printf("-----------------------------------------\n");
	while (iterator != NULL){
		print_node(iterator);
		iterator = iterator -> next;
	}
}

void print_node(Node *node){
		printf("Type: %s\t Socket: %d\t File: %s\t IP: %s\t Size: %d\t Content-Type: %s\t ARR : %s\nFilePath : %s\nCurrent Dir : %s\n\n", node -> request_type, node -> acceptfd, node -> file_name, node -> client_ip, node -> file_size, node -> content_type, node -> arrival_time, node -> file_path, node -> current_dir);
}

/* 
 * HELP FOR THE myhttpd -h OPTION 
 * */
void usage()
{
	fprintf(stderr, "Usage Summary: myhttpd -d -h -l filename -p portno -r rootdirectory -t threadwaittime -n threadnumber -s scheduling\n");
	fprintf(stderr, "Give -d parameter in args to on debugging mode\n");
	fprintf(stderr, "Give -h parameter to display the summary\n");
	fprintf(stderr, "Give -l and then filename of logging file for example: logging.txt\n");
	fprintf(stderr, "Give -p and then portno to change default port number for example: -p 8080\n");
	fprintf(stderr, "Give -r and then root directory to change default value of root directory for example: -r home/workspace/Myhttpd/\n");
	fprintf(stderr, "Give -t and then thread time to change default wait time of scheduler thread for example: -t 30\n");
	fprintf(stderr, "Give -n and then thread numbers to change the default value of threads for example: -n 10\n");
	fprintf(stderr, "Give -s and then scheduling name to change default scheduling for example: -s SJF\n");
	fprintf(stderr, "Press Ctrl+c anytime to exit the server\n");
	exit(1);
}
