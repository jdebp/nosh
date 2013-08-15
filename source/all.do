#!/bin/sh -e
commands1="exec service-control service-dt-scanner service-is-ok service-is-up service-status service-show tai64n tai64nlocal session-manager"
commands8="init system-manager"
extra_1_manpages="nosh chdir chroot clearenv cyclog envdir envuidgid fdmove fdredir line-banner local-socket-accept local-socket-listen login-banner login-process login-prompt open-controlling-tty pty-get-tty pty-run read-conf recordio setenv setlock setuidgid-fromenv setuidgid setpgrp setsid softlimit tcp-socket-accept tcp-socket-listen ulimit umask unsetenv vc-get-tty vc-reset-tty appendpath prependpath false true system-control service-manager set-dynamic-hostname setup-machine-id pipe ucspi-socket-rules-check"
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
