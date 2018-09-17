# vim: set filetype=sh:
# service list
# user-mode TTYs
user_tty "vc1-tty"
user_tty "vc2-tty"
user_tty "vc3-tty"
service_with_dedicated_logger "terminal-emulator@vc1"
service_with_dedicated_logger "terminal-emulator@vc2"
service_with_dedicated_logger "terminal-emulator@vc3"

login_service_with_dedicated_logger "vc1-tty"
login_service_with_dedicated_logger "vc2-tty"
login_service_with_dedicated_logger "vc3-tty"
# TTY ancillaries
service_with_dedicated_logger "console-input-method@head0"
service_with_dedicated_logger "console-multiplexor@head0"
service_with_dedicated_logger "console-fb-realizer@head0"
