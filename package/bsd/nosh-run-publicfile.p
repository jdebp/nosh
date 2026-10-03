# vim: set filetype=sh:
# non-log users
non_file_owning_user "publicfile"
# common targets, services, and sockets
socket_with_dedicated_logger "finger6d"
socket_with_dedicated_logger "ftp4d"
socket_with_dedicated_logger "gopher6d"
socket_with_dedicated_logger "gemini6d"
socket_with_dedicated_logger "http6d"
socket_with_dedicated_logger "https6d"
socket_with_dedicated_logger "ident6d"
socket_with_dedicated_logger "nicname6d"
# BSD-specific
