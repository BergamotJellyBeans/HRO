#!/bin/bash

set -e

case "${1:-}" in

    start)
        systemctl start hro-png.service
        systemctl start hro-engine.service
        ;;

    stop)
        systemctl stop hro-engine.service
        systemctl stop hro-png.service
        ;;

    restart)
        systemctl restart hro-png.service
        systemctl restart hro-engine.service
        ;;

    status)
        systemctl is-active hro-png.service
        systemctl is-active hro-engine.service
        ;;

    shutdown)
        systemctl poweroff
        ;;

    *)
        echo "Usage: $0 {start|stop|restart|status|shutdown}" >&2
        exit 1
        ;;
esac
