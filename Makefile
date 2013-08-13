CFLAGS =	-I$(LIBEVENT)/include -Wall

LIBS =		-levent

chat-server: chat-server.c chat-server.h queue.h
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f chat-server *.o
