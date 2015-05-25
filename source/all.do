#!/bin/sh -e
service_lists="../package/common-services ../package/bsd-services ../package/linux-services ../package/common-sysinit-services ../package/bsd-sysinit-services ../package/linux-sysinit-services ../package/common-kernel-vt-services ../package/bsd-kernel-vt-services ../package/linux-kernel-vt-services ../package/user-vt-services ../package/bsd-ttys ../package/linux-ttys ../package/common-ttys ../package/common-sockets ../package/bsd-sockets ../package/linux-sockets"
target_lists="../package/standard-targets"
mount_lists="../package/sysinit-mounts"
redo-ifchange version.h
redo-ifchange services/ttylogin@.service services/klogd.socket
redo-ifchange ${service_lists} ${target_lists} ${mount_lists}
cat ${service_lists} |
while read i
do
	echo services/"$i" services/"cyclog@$i"
done | xargs redo-ifchange
redo-ifchange ${target_lists}
cat ${target_lists} |
while read i
do
	echo targets/"$i"
done | xargs redo-ifchange
cat ${mount_lists} |
while read i
do
	echo services/fsck@"$i"
	echo services/mount@"$i"
done | xargs redo-ifchange
commands1="exec service-control system-control console-terminal-emulator"
commands1="exec service-control system-control console-terminal-emulator"
commands8="init system-manager"
extra_1_manpages="appendpath chdir chroot clearenv console-convert-kbdmap console-fb-realizer console-multiplexor console-ncurses-realizer console-resize cyclog detach-controlling-tty emergency-login envdir envuidgid false fdmove fdredir fifo-listen foreground klog-read line-banner local-datagram-socket-listen local-stream-socket-accept local-stream-socket-listen login-banner login-process login-prompt make-private-fs nosh open-controlling-tty pause pipe prependpath pty-get-tty pty-run read-conf recordio service-dt-scanner service-is-ok service-is-up service-manager service-show service-status session-manager set-dynamic-hostname set-mount-object setenv setlock setpgrp setsid setuidgid setuidgid-fromenv setup-machine-id softlimit syslog-read tai64n tai64nlocal tcp-socket-accept tcp-socket-listen true ttylogin-starter ucspi-socket-rules-check udp-socket-listen udp-socket-listen ulimit umask unsetenv unshare userenv vc-get-tty vc-reset-tty"
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
