# vim: set filetype=sh:
# dedicated user accounts
file_owning_user "cron" "/var/spool/cron"
directory "cron" "/var/spool/cron" "0755"
directory "cron" "/var/spool/cron/tmp" "0700"
directory "cron" "/var/spool/cron/crontabs" "0700"
# Common
service_with_dedicated_logger "bcron-start"
service_with_dedicated_logger "bcron-update"
socket_with_dedicated_logger "bcron-spool"
# BSD-specific
