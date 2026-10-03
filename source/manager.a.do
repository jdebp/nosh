#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
objects='object/common-manager.o object/api_mounts.o object/api_symlinks.o object/service-manager-client.o object/service-manager-socket.o object/system-control.o object/system-state-change.o object/system-control-status.o object/system-control-cat.o object/system-control-escape.o object/start-stop-service.o object/enable-disable-preset.o object/system-control-service-env.o'
redo-ifchange ./archive ${objects}
./archive "$3" ${objects}
