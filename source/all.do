#!/bin/sh -e
redo-ifchange version.h
commands1="exec service-control session-manager tai64n tai64nlocal console-terminal-emulator"
commands8="init system-manager"
extra_1_manpages="appendpath chdir chroot clearenv console-convert-kbdmap console-fb-realizer console-multiplexor console-ncurses-realizer cyclog detach-controlling-tty envdir envuidgid false fdmove fdredir foreground line-banner local-datagram-socket-listen local-stream-socket-accept local-stream-socket-listen login-banner login-process login-prompt make-private-fs nosh open-controlling-tty pause pipe prependpath pty-get-tty pty-run read-conf recordio service-dt-scanner service-is-ok service-is-up service-manager service-show service-status set-dynamic-hostname set-mount-object setenv setlock setpgrp setsid setuidgid setuidgid-fromenv setup-machine-id softlimit system-control tcp-socket-accept tcp-socket-listen true ttylogin-starter ucspi-socket-rules-check udp-socket-listen udp-socket-listen ulimit umask unsetenv unshare userenv vc-get-tty vc-reset-tty"
extra_8_manpages=""
redo-ifchange ${commands1} ${commands8}
for i in ${commands1} ${extra_1_manpages}
do 
	echo "$i.1" "$i.html"
done | xargs redo-ifchange
for i in ${commands8} ${extra_8_manpages}
do 
	echo "$i.8" "$i.html"
done | xargs redo-ifchange
