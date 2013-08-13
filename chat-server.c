/*
* The MIT License (MIT)
* Copyright (c) 2011 Jason Ish
* Copyright (c) 2013 Agustí Moll
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use, copy,
* modify, merge, publish, distribute, sublicense, and/or sell copies
* of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
* HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
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

/* Libevent. */
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>

#include "queue.h"
#include "chat-server.h"


/**
 * The head of our tailq of all connected clients.  This is what will
 * be iterated to send a received message to all connected clients.
 */
TAILQ_HEAD(, client) client_tailq_head;


/**
 * Called by libevent when there is data to read.
 */
void buffered_on_read(struct bufferevent *bev, void *arg)
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
			printf("Client from %d, now is %s.\n", this_client->fd, nickname_temp);
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

	char *nn;
	char anonymous[] = "Anonymous";

	if(c->status > STATUS_NICKNAME){
		nn = c->nickname;
	} else {
		nn = anonymous;
	}
	printf("Remove %s from %d.\n",nn,c->fd);

	bufferevent_free(c->buf_ev);
	close(c->fd);
	free(c->nickname);
	free(c);	

	/* Remove the client from the tailq. */
	TAILQ_REMOVE(&client_tailq_head, c, entries);
}


void on_accept(struct evconnlistener *listener, evutil_socket_t client_fd,
    struct sockaddr *sa, int socklen, void *user_data)
{
	struct event_base *evbase = user_data;
	struct client *client;
	const char welcome_message[]=WELCOME_MESSAGE;
	struct sockaddr_in *sai;


	sai = (struct sockaddr_in *) sa;
	/* We've accepted a new client, create a client object. */
	client = calloc(1, sizeof(*client));
	if (client == NULL)
		err(1, "malloc failed");
	client->fd = client_fd;
	client->status = STATUS_NICKNAME;

	client->buf_ev = bufferevent_socket_new(evbase, client_fd, BEV_OPT_CLOSE_ON_FREE);
	if (!client->buf_ev) {
		fprintf(stderr, "Error constructing bufferevent!");
		event_base_loopbreak(evbase);
		return;
	}
	bufferevent_setcb(client->buf_ev, buffered_on_read, NULL,
	    buffered_on_error, client);

	/* We have to enable it before our callbacks will be
	 * called. */
	bufferevent_enable(client->buf_ev, EV_READ);

	/* Add the new client to the tailq. */
	TAILQ_INSERT_TAIL(&client_tailq_head, client, entries);

	printf("Accepted connection from %s\n", inet_ntoa(sai->sin_addr));

	bufferevent_write(client->buf_ev, welcome_message ,  sizeof(welcome_message));

}

static void signal_sigint(evutil_socket_t sig, short events, void *user_data)
{
	struct event_base *evbase = user_data;
	struct client *client;
	const char goodbye_message[]=GOODBYE_MESSAGE;

	printf("Caught an interrupt signal, close all clients.\n");

	TAILQ_FOREACH(client, &client_tailq_head, entries) {
		bufferevent_write(client->buf_ev, goodbye_message,  strlen(goodbye_message));
		bufferevent_flush(client->buf_ev, EV_READ|EV_WRITE, BEV_FLUSH);
		close_client(client);
	}

	event_base_loopexit(evbase, NULL);
}

int main(int argc, char **argv)
{
	struct evconnlistener *listener;
	struct sockaddr_in sin;
	struct event_base *evbase;
	struct event *signal_event;

	/* Initialize libevent. */

	printf("Initialize event_base.\n");

    evbase = event_base_new();
	if (!evbase) {
		fprintf(stderr, "Could not initialize libevent!\n");
		return 1;
	}

	/* Initialize the tailq. */
	TAILQ_INIT(&client_tailq_head);

	/* Create listening with evconnlistener */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(SERVER_PORT);

	printf("Bind port %d.\n",SERVER_PORT);

	listener = evconnlistener_new_bind(evbase, on_accept, (void *)evbase,
	    LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE, -1,
	    (struct sockaddr*)&sin,
	    sizeof(sin));

	if (!listener) {
		fprintf(stderr, "Could not create a listener!\n");
		return 1;
	}

	/* Add signal SIGINT */

	printf("Program signal SIGINT.\n");

	signal_event = evsignal_new(evbase, SIGINT, signal_sigint, (void *)evbase);

	if (!signal_event || event_add(signal_event, NULL)<0) {
		fprintf(stderr, "Could not create/add a signal event!\n");
		return 1;
	}

	/* Start the event loop. */

	printf("Start event loop - Waiting for connections.\n");

	event_base_dispatch(evbase);

	/* When stop loop */

	printf("End of Loop.\n");

	evconnlistener_free(listener);
	event_free(signal_event);
	event_base_free(evbase);

	printf("%s\n",GOODBYE_MESSAGE);
	return 0;
}
