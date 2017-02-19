#include <iostream>
#include <stdio.h>
#include <sys/socket.h> /* for socket(), connect(), send(), recv() */
#include <arpa/inet.h> /* for sockaddr_in and inet_addr */
#include <stdlib.h> /* for atoi */
#include <string.h> /* for memset() */
#include <unistd.h> /* for close() */
#include <netdb.h>
#include <sys/types.h>
#include <linux/joystick.h>
#include <linux/input.h>
#include <mqueue.h>
#include <errno.h>
#include <signal.h>


#define MAX_CLIENTS	100

static unsigned int cli_count = 0; 
static int uid = 10; 

/* Client structrure */ 
typedef struct {
	struct sockaddr_in addr; /* client remote address */ 
	int connfd; /* Connection file descritpor */ 
	int uid; 
	char name[32]; 
} client_t; 

client_t* clients[MAX_CLIENTS]; 

/* Add client to queue */ 
void queue_add(client_t* cl) {
	int i; 
	for(i = 0; i < MAX_CLIENTS; ++i) {
		if(!clients[i]) {
			clients[i] = cl; 
			return;
		}
	}
}

/* Delete client from queue */ 
void queue_delete(int uid) {
	int i; 
	for(i = 0; i < MAX_CLIENTS; ++i) {
		if(clients[i]) {
			if(clients[i]->uid == uid) {
				clients[i] = NULL; 
				return;
			}
		}
	}
}

/* Send message to all clients but the sender */ 
void send_message(char* s, int uid) {
	int i; 
	for(i = 0; i < MAX_CLIENTS; ++i) {
		if(clients[i]) {
			if(clients[i]->uid != uid) {
				write(clients[i]->connfd, s, strlen(s));
			}
		}
	}
}

/* Send message to all clients */ 
void send_message_all(char* s) {
	int i; 
	for(i = 0; i < MAX_CLIENTS; ++i) {
		if(clients[i]) {
			write(clients[i]->connfd, s, strlen(s));
		}
	}
}

/* Send message to sender */ 
void send_message_self(const char* s, int connfd) {
	write(connfd, s, strlen(s));
}

/* Send message to client */ 
void send_message_client(char* s, int uid) {
	int i; 
	for(i = 0; i < MAX_CLIENTS; ++i) {
		if(clients[i]) {
			if(clients[i]->uid == uid) {
				write(clients[i]->connfd, s, strlen(s));
			}
		}
	}
}

/* Send list of active clients */ 
void send_active_clients(int conffd) {
	int i; 
	char s[64]; 
	for(i = 0; i < MAX_CLIENTS; ++i) {
		if(clients[i]) {
			sprintf(s, "<<CLIENT %d | %s \r\n", clients[i]->uid, clients[i]->name);
			send_message_self(s, conffd);
		}
	}
}

/* Strip CRLF */ 
void strip_newline(char* s) {
	while(*s != '\0') {
		if(*s == '\r' || *s == '\n') {
			*s = '\0';
		}
		s++;
	}
}


/* Print ip address */ 
void print_client_addr(struct sockaddr_in addr) {
	printf("%d.%d.%d.%d", 
		addr.sin_addr.s_addr & 0xFF, 
		(addr.sin_addr.s_addr & 0xFF00) >> 8, 
		(addr.sin_addr.s_addr & 0xFF0000) >> 16, 
		(addr.sin_addr.s_addr & 0xFF000000) >> 24);
}


/* Handle all communication with the client */ 

void* handle_client(void* arg) {
	char buff_out[1024]; 
	char buff_in[1024]; 
	int rlen; 

	cli_count++; 
	client_t* cli = (client_t*) arg;

	printf("<<ACCEPT ");
	print_client_addr(cli->addr);
	printf(" REFERENCED BY %d\n", cli->uid);

	sprintf(buff_out, "<<JOIN, HELLO %s\r\n", cli->name);
	send_message_all(buff_out);


	
	// Make the nonblocking 
	
	int flags = fcntl(cli->connfd, F_GETFL, 0);
	if(!(flags < 0)) {
		flags = flags | O_NONBLOCK;
		fcntl(cli->connfd, F_SETFL, flags);
	} else {
		perror("fcntl \n");
	} 
	

	/* Recieve input from client */ 
	while((rlen = read(cli->connfd, buff_in, sizeof(buff_in))) > 0) {
		buff_in[rlen] = '\0'; 
		buff_out[0] = '\0';
		strip_newline(buff_in);

		/* Ignore empty buffer */ 
		if(!strlen(buff_in)) {
			continue; 
		} 

		/* Special options */ 
		if(buff_in[0] ='\\') {
			char *command, *param; 
			command = strtok(buff_in, " ");
			if(!strcmp(command, "\\QUIT")) {
				break;
			} else if(!strcmp(command, "\\PING")) {
				send_message_self("<<PONG\r\n", cli->connfd);
			} else if(!strcmp(command, "\\NAME")) {
				param = strtok(NULL, " ");
				if(param) {
					char* old_name = strdup(cli->name);
					strcpy(cli->name, param);
					sprintf(buff_out, "<<RENAME, %s TO %s \r\n", old_name, cli->name);\
					free(old_name);
					send_message_all(buff_out);
				} else {
					send_message_self("<<NAME CANNOT BE NULL\r\n", cli->connfd);
				}
			} else if(!strcmp(command, "\\PRIVATE")) {
				param = strtok(NULL, " ");
				if(param) {
					int uid = atoi(param);
					param = strtok(NULL, " ");
					if(param) {
						sprintf(buff_out, "[PM][%s]", cli->name);
						while(param != NULL) {
							strcat(buff_out, " ");
							strcat(buff_out, param);
							param = strtok(NULL, " ");
						}

						strcat(buff_out, "\r\n");
						send_message_client(buff_out, uid);
					} else {
						send_message_self("<<MESSAGE CANNOT BE NULL\r\n", cli->connfd);
					}
				} else {
					send_message_self("<<REFERENCE CANNOT BE NULL\r\n", cli->connfd);
				}
			} else if(!strcmp(command, "\\ACTIVE")) {
				sprintf(buff_out, "<<CLIENTS %d\r\n", cli_count);
				send_message_self(buff_out, cli->connfd);
				send_active_clients(cli->connfd);
			} else if(!strcmp(command, "\\HELP")) {
				strcat(buff_out, "\\QUIT     Quit chatroom\r\n");
				strcat(buff_out, "\\PING     Server test\r\n");
				strcat(buff_out, "\\NAME     <name> Change nickname\r\n");
				strcat(buff_out, "\\PRIVATE  <reference> <message> Send private message\r\n");
				strcat(buff_out, "\\ACTIVE   Show active clients\r\n");
				strcat(buff_out, "\\HELP     Show help\r\n");
				send_message_self(buff_out, cli->connfd);
			} else {
								send_message_self("<<UNKOWN COMMAND\r\n", cli->connfd);
			}
		} else {
				sprintf(buff_out, "[%s] %s\r\n", cli->name, buff_in);
				send_message(buff_out, cli->uid);

		}
	}


	/* Close connection */ 
	close(cli->connfd);
	sprintf(buff_out, "<<LEAVE BYE %s\r\n", cli->name);
	send_message_all(buff_out);

	/* DELETE client from queue and yeld thread */ 
	queue_delete(cli->uid);
	printf("<<LEAVE");
	print_client_addr(cli->addr);
	printf(" REFERENCED BY %d\n", cli->uid);
	free(cli);
	cli_count--; 
	pthread_detach(pthread_self());

	return NULL;

}


int main(int argc, char* argv[]) {

	
	int socket_fd = 0, conn_fd = 0; 
	struct sockaddr_in server_addr; 
	struct sockaddr_in client_addr; 
	pthread_t tid; 

	/* Socket settings */ 
	socket_fd = socket(AF_INET, SOCK_STREAM, 0); 
	server_addr.sin_family = AF_INET; 
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(1111);

	/* Bind */ 
	if(bind(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("socket binding failed");
		exit(1);
	}


	/*Listen */ 
	if(listen(socket_fd, 10) < 0) {
		perror("socket listening failed");
		exit(1);
	} 

	printf("<<SERVER STARTED>> \n");

	/* Accept connection */ 

	while(1) {
		socklen_t clilen = sizeof(client_addr);
		conn_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &clilen);


		/* Check if max client is reached */ 
		if((cli_count+1) == MAX_CLIENTS) {
			printf("<<MAX CLIENTS REACHED \n");
			printf("<<REJECT ");
			print_client_addr(client_addr);
			printf("\n");
			close(conn_fd);
			continue;
		} 


		/* Client settings */ 
		client_t* cli = (client_t*)malloc(sizeof(client_t));
		cli->addr = client_addr; 
		cli->connfd = conn_fd; 
		cli->uid = uid++;

		sprintf(cli->name, "%d", cli->uid);

		/* Add client to queue and fork thread */ 
		queue_add(cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		/* Reduce CP USAGE */ 

		sleep(1);

	}
	

    





    return 0;
}

