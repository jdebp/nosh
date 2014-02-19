#!/bin/sh -e
objects="builtins.o exec.o chdir.o chroot.o cyclog.o umask.o local-datagram-socket-listen.o local-stream-socket-accept.o local-stream-socket-listen.o login-banner.o login-prompt.o login-process.o nosh.o open-controlling-tty.o pty-get-tty.o pty-run.o vc-get-tty.o vc-reset-tty.o setuidgid-fromenv.o setuidgid.o setpgrp.o setsid.o setenv.o unsetenv.o udp-socket-listen.o tcp-socket-accept.o tcp-socket-listen.o ulimit.o envuidgid.o line-banner.o clearenv.o fdmove.o fdredir.o read-conf.o recordio.o setlock.o appendpath.o prependpath.o envdir.o true.o false.o common-manager.o service.o service-manager.o set-dynamic-hostname.o setup-machine-id.o convert-systemd-units.o pipe.o ucspi-socket-rules-check.o pause.o"
redo-ifchange ./archive ${objects}
./archive "$3" ${objects}
