#!/bin/sh -e
objects="posix_clearenv.o nmount.o iovec.o popt.o popt-bool.o popt-named.o popt-signed.o popt-simple.o popt-compound.o popt-string.o popt-string-list.o popt-table.o popt-top-table.o popt-unsigned.o open_exec.o open_lockfile.o open_lockfile_or_wait.o pipe_close_on_exec.o socket_close_on_exec.o read-file.o basename.o sane.o raw.o ttyname.o tcgetattr.o tcsetattr.o tcgetwinsz.o tcsetwinsz.o listen.o unpack.o is_jail.o is_set_hostname_allowed.o service-manager-client.o read_env_file.o"
redo-ifchange ./archive ${objects}
./archive "$3" ${objects}
