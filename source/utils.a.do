#!/bin/sh -e
objects="CompositeFont.o FileDescriptorOwner.o FramebufferIO.o GraphicsInterface.o ProcessEnvironment.o SignalManagement.o SoftTerm.o UnicodeClassification.o basename.o begins_with.o bundle_creation.o comment.o control_groups.o dirname.o ends_in.o fstab_options.o home_dir.o host_id.o iovec.o is_bool.o is_jail.o is_set_hostname_allowed.o keycode_to_map.o listen.o machine_id.o nmount.o open_exec.o open_lockfile.o open_lockfile_or_wait.o pack.o pipe_close_on_exec.o popt-bool.o popt-compound.o popt-compound-2arg.o popt-named.o popt.o popt-signed.o popt-simple.o popt-string-list.o popt-string-pair-list.o popt-string-pair.o popt-string.o popt-table.o popt-top-table.o popt-unsigned.o process_env_dir.o raw.o read_env_file.o read_line.o read-file.o runtime_dir.o sane.o service-manager-client.o service-manager-socket.o setprocargv.o setprocname.o socket_close_on_exec.o socket_connect.o socket_set_option.o signame.o split_list.o subreaper.o systemd_names.o tai64.o terminal_database.o tcgetattr.o tcgetwinsz.o tcsetattr.o tcsetwinsz.o tolower.o trim.o ttyname.o unpack.o usb_ident_to_keymap.o val.o wait.o"
other_objects=""
case "`uname`" in
Linux)	more_objects="kqueue_linux.o";;
*BSD)	more_objects="";;
esac
redo-ifchange ./archive ${objects} ${more_objects}
./archive "$3" ${objects} ${more_objects}
