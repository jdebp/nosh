#!/bin/sh -e
commands1="exec service-control service-dt-scanner service-is-ok service-is-up service-show service-status session-manager tai64n tai64nlocal"
commands8="init system-manager"
extra_1_manpages="appendpath chdir chroot clearenv cyclog envdir envuidgid false fdmove fdredir foreground line-banner local-datagram-socket-listen local-stream-socket-accept local-stream-socket-listen login-banner login-process login-prompt make-private-fs nosh open-controlling-tty pause pipe prependpath pty-get-tty pty-run read-conf recordio service-manager set-dynamic-hostname set-mount-object setenv setlock setpgrp setsid setuidgid setuidgid-fromenv setup-machine-id softlimit system-control tcp-socket-accept tcp-socket-listen true ucspi-socket-rules-check udp-socket-listen udp-socket-listen ulimit umask unsetenv unshare userenv vc-get-tty vc-reset-tty"
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
