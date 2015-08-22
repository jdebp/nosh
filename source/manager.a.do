#!/bin/sh -e
objects="common-manager.o api_mounts.o api_symlinks.o service-manager-socket.o system-control.o system-state-change.o system-control-status.o system-control-cat.o start-stop-service.o enable-disable-preset.o system-control-service-env.o"
redo-ifchange ./archive ${objects}
./archive "$3" ${objects}