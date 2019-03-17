#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
objects="BaseTUI.o CompositeFont.o ECMA48Decoder.o ECMA48Output.o FileDescriptorOwner.o FramebufferIO.o GraphicsInterface.o InputFIFO.o IPAddress.o MapColours.o ProcessEnvironment.o SignalManagement.o SoftTerm.o TerminalCapabilities.o TUIDisplayCompositor.o TUIInputBase.o TUIOutputBase.o TUIVIO.o UTF8Decoder.o UnicodeClassification.o UserEnvironmentSetter.o VirtualTerminalBackEnd.o basename.o begins_with.o bundle_creation.o comment.o control_groups.o dirname.o ends_in.o fstab_options.o getaddrinfo_unix.o home_dir.o host_id.o iovec.o is_bool.o is_jail.o is_set_hostname_allowed.o kbdmap_bsd_keycode_to_index.o kbdmap_default.o kbdmap_evdev_keycode_to_index.o kbdmap_usb_ident_to_index.o listen.o machine_id.o nmount.o open_exec.o open_lockfile.o open_lockfile_or_wait.o pack.o pipe_close_on_exec.o popt-bool.o popt-bool-string.o popt-compound.o popt-compound-2arg.o popt-integral.o popt-named.o popt.o popt-signed.o popt-simple.o popt-string-list.o popt-string-pair-list.o popt-string-pair.o popt-string.o popt-table.o popt-top-table.o popt-unsigned.o process_env_dir.o quote.o raw.o read_env_file.o read_line.o read-file.o runtime_dir.o sane.o setprocargv.o setprocenvv.o setprocname.o socket_close_on_exec.o socket_connect.o socket_set_option.o signame.o split_list.o subreaper.o systemd_names.o tai64.o terminal_database.o tcgetattr.o tcgetwinsz.o tcsetattr.o tcsetwinsz.o tolower.o trim.o ttyname.o unpack.o val.o wait.o"
other_objects=""
case "`uname`" in
Linux)	more_objects="kqueue_linux.o";;
*BSD)	more_objects="";;
esac
redo-ifchange ./archive ${objects} ${more_objects}
./archive "$3" ${objects} ${more_objects}
