A simple chat server using libevent2
-------------------------------------------------------------------------------
This chat is based with https://github.com/jasonish/libevent-examples/tree/master/chat-server by Jason Ish.

Changes:

* Use evconnlistener_new_bind for bind port, much simpler.
* Add evconnlistener_free because in some cases port is binded.
* Add singal in SIGINT.


USAGE

    Run the server:

        ./chat-server

    In client:
    	telnet localhost 5555
    or
    	nc localhost 5555

--
Agustí Moll <agusti@biruji.org>
