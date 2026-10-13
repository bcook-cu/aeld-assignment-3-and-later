#!/bin/sh

DAEMON=/usr/bin/aesdsocket

case "$1" in
    start)
        echo "Starting aesdsocket"
        start-stop-daemon --start -x "$DAEMON" -- -d
        ;;
    stop)
        echo "Stopping aesdsocket"
        start-stop-daemon --stop -x "$DAEMON" --signal TERM
        ;;
    *)
        echo "Usage: $0 {start|stop}" >&2
        exit 1
        ;;
esac

exit $?

