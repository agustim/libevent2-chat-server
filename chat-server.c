#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/time.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <signal.h>

#include "queue.h"
#include "chat-server.h"

/* Libevent. */
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>


static struct event_base *evbase;


/**
 * The head of our tailq of all connected clients.  This is what will
 * be iterated to send a received message to all connected clients.
 */
TAILQ_HEAD(, client) client_tailq_head;

/**
 * Set a socket to non-blocking mode.
 */
int
setnonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return -1;

	return 0;
}

/**
 * Called by libevent when there is data to read.
 */
void
buffered_on_read(struct bufferevent *bev, void *arg)
{
	struct client *this_client = arg;
	struct client *client;

	uint8_t data[8192];
	char *nickname_temp;
	size_t n;

	/* Read 8k at a time and send it to all connected clients. */
	for (;;) {
		n = bufferevent_read(bev, data, sizeof(data));
		if (n <= 0) {
			break;
		}

		if (this_client->status == STATUS_CHAT) {
			TAILQ_FOREACH(client, &client_tailq_head, entries) {
				if (client != this_client) {
					bufferevent_write(client->buf_ev, this_client->nickname,  strlen(this_client->nickname));
					bufferevent_write(client->buf_ev, data,  n);
				}
			}
		}
		
		if (this_client->status == STATUS_NICKNAME) {
			this_client->nickname = malloc(n+4);
			if (this_client->nickname == NULL)
				err(1, "malloc (this_client->nickname) failed");
			nickname_temp = malloc(n);
			if (nickname_temp == NULL)
				err(1, "malloc (nickname_temp) failed");
			bzero(nickname_temp,n);
			memcpy(nickname_temp,data,n-1);
			sprintf(this_client->nickname,"[%s]:",nickname_temp);
			free(nickname_temp);
			this_client->status = STATUS_CHAT;
		}

	}

}
/**
 * Called by libevent when there is an error on the underlying socket
 * descriptor.
 */
void buffered_on_error(struct bufferevent *bev, short what, void *arg)
{
	struct client *client = (struct client *)arg;

	if (what & BEV_EVENT_EOF) {
		/* Client disconnected, remove the read event and the
		 * free the client structure. */
		printf("Client disconnected.\n");
	}
	else {
		warn("Client socket error, disconnecting.\n");
	}
	close_client(client);

}

void close_client(struct client *c){

	if(c->status > STATUS_NICKNAME)
		printf("Remove %s from %d.\n",c->nickname,c->fd);
	else 
		printf("Remove Anonymous, form %d.\n", c->fd);

	/* Remove the client from the tailq. */
	TAILQ_REMOVE(&client_tailq_head, c, entries);

	bufferevent_free(c->buf_ev);
	close(c->fd);
	free(c->nickname);
	free(c);	
}

/**
 * This function will be called by libevent when there is a connection
 * ready to be accepted.
 */
void
on_accept(int fd, short ev, void *arg)
{
	int client_fd;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	struct client *client;
	const char welcome_message[]=WELCOME_MESSAGE;

	client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
	if (client_fd < 0) {
		warn("accept failed");
		return;
	}

	/* Set the client socket to non-blocking mode. */
	if (setnonblock(client_fd) < 0)
		warn("failed to set client socket non-blocking");

	/* We've accepted a new client, create a client object. */
	client = calloc(1, sizeof(*client));
	if (client == NULL)
		err(1, "malloc failed");
	client->fd = client_fd;
	client->status = STATUS_NICKNAME;

	client->buf_ev = bufferevent_socket_new(evbase, client_fd, 0);
	bufferevent_setcb(client->buf_ev, buffered_on_read, NULL,
	    buffered_on_error, client);

	/* We have to enable it before our callbacks will be
	 * called. */
	bufferevent_enable(client->buf_ev, EV_READ);

	/* Add the new client to the tailq. */
	TAILQ_INSERT_TAIL(&client_tailq_head, client, entries);

	printf("Accepted connection from %s\n", inet_ntoa(client_addr.sin_addr));

	bufferevent_write(client->buf_ev, welcome_message ,  sizeof(welcome_message));

}

static void
signal_sigint(evutil_socket_t sig, short events, void *user_data)
{
	struct event_base *evbase = user_data;
	struct client *client;
	struct timeval delay = { 0, 0 };
	const char goodbye_message[]=GOODBYE_MESSAGE;

	printf("Caught an interrupt signal, close all clients.\n");

	TAILQ_FOREACH(client, &client_tailq_head, entries) {
		bufferevent_write(client->buf_ev, goodbye_message,  strlen(goodbye_message));
		close_client(client);
	}

	event_base_loopexit(evbase, &delay);
}

int
main(int argc, char **argv)
{
	int listen_fd;
	struct sockaddr_in listen_addr;
	struct event ev_accept;
	struct event *signal_event;
	int reuseaddr_on;

	/* Initialize libevent. */
    evbase = event_base_new();

	/* Initialize the tailq. */
	TAILQ_INIT(&client_tailq_head);

	/* Create our listening socket. */
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0)
		err(1, "listen failed");
	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(SERVER_PORT);
	if (bind(listen_fd, (struct sockaddr *)&listen_addr,
		sizeof(listen_addr)) < 0)
		err(1, "bind failed");
	if (listen(listen_fd, 5) < 0)
		err(1, "listen failed");
	reuseaddr_on = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, 
	    sizeof(reuseaddr_on));

	/* Set the socket to non-blocking, this is essential in event
	 * based programming with libevent. */
	if (setnonblock(listen_fd) < 0)
		err(1, "failed to set server socket to non-blocking");

	/* Add signal SIGINT */
	signal_event = evsignal_new(evbase, SIGINT, signal_sigint, (void *)evbase);

	if (!signal_event || event_add(signal_event, NULL)<0) {
		fprintf(stderr, "Could not create/add a signal event!\n");
		return 1;
	}
	/* We now have a listening socket, we create a read event to
	 * be notified when a client connects. */
    event_assign(&ev_accept, evbase, listen_fd, EV_READ|EV_PERSIST, on_accept, NULL);
	event_add(&ev_accept, NULL);

	/* Start the event loop. */
	event_base_dispatch(evbase);
	event_free(signal_event);
	event_base_free(evbase);

	printf("Goodbye!\n");
	return 0;
}
