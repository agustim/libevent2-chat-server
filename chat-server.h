/* chat-server.h */

#include "queue.h"


#ifndef	_CHAT_SERVER_H_
#define	_CHAT_SERVER_H_

#define SERVER_PORT 5556
#define STATUS_INIT 0
#define STATUS_NICKNAME 1
#define STATUS_CHAT 2

#define WELCOME_MESSAGE "Enter your nickname:"
#define GOODBYE_MESSAGE "See you soon!\n"

struct client {
	/* The socket. */
	int fd;

	/* The bufferedevent for this client. */
	struct bufferevent *buf_ev;

	int status;
	char *nickname;
	/*
	 * This holds the pointers to the next and previous entries in
	 * the tail queue.
	 */
	TAILQ_ENTRY(client) entries;
} ;

void close_client(struct client *);

#endif