{
	two=$2;
	gsub("^\"|\"$","",two);
	if ("logfile_owning_user" == $1) {
		;
	} else
	if ("file_owning_user" == $1) {
		;
	} else
	if ("non_file_owning_user" == $1) {
		;
	} else
	if ("service_with_dedicated_logger" == $1) {
		print "@exec","system-control","preset",two ".service";
		print "@exec","system-control","preset","--prefix","cyclog@",two ".service";
		print "@exec","system-control","reset",two ".service";
		print "@exec","system-control","reset","cyclog@" two ".service";
		print "@unexec","system-control","disable",two ".service";
		print "@unexec","system-control","disable","cyclog@" two ".service";
		print "@unexec","system-control","unload-when-stopped",two ".service";
		print "@unexec","system-control","unload-when-stopped","cyclog@" two ".service";
		print "@unexec","system-control","stop",two ".service";
		print "@unexec","system-control","stop","cyclog@" two ".service";
	} else
	if ("login_service_with_dedicated_logger" == $1) {
		print "@exec","system-control","preset","--ttys","--prefix","ttylogin@",two ".service";
		print "@exec","system-control","preset","--ttys","--prefix","cyclog@ttylogin@",two ".service";
		print "@exec","system-control","reset","ttylogin@" two ".service";
		print "@exec","system-control","reset","cyclog@ttylogin@" two ".service";
		print "@unexec","system-control","disable","ttylogin@" two ".service";
		print "@unexec","system-control","disable","cyclog@ttylogin@" two ".service";
		print "@unexec","system-control","unload-when-stopped","ttylogin@" two ".service";
		print "@unexec","system-control","unload-when-stopped","cyclog@ttylogin@" two ".service";
		print "@unexec","system-control","stop","ttylogin@" two ".service";
		print "@unexec","system-control","stop","cyclog@ttylogin@" two ".service";
	} else
	if ("ktty_login_service_with_dedicated_logger" == $1) {
		print "@exec","system-control","preset","--ttys","--prefix","ttylogin@","ttyC" two ".service";
		print "@exec","system-control","preset","--ttys","--prefix","cyclog@ttylogin@","ttyC" two ".service";
		print "@exec","system-control","reset","ttylogin@ttyC" two ".service";
		print "@exec","system-control","reset","cyclog@ttylogin@ttyC" two ".service";
		print "@unexec","system-control","disable","ttylogin@ttyC" two ".service";
		print "@unexec","system-control","disable","cyclog@ttylogin@ttyC" two ".service";
		print "@unexec","system-control","unload-when-stopped","ttylogin@ttyC" two ".service";
		print "@unexec","system-control","unload-when-stopped","cyclog@ttylogin@ttyC" two ".service";
		print "@unexec","system-control","stop","ttylogin@ttyC" two ".service";
		print "@unexec","system-control","stop","cyclog@ttylogin@ttyC" two ".service";
	} else
	if ("socket_with_dedicated_logger" == $1) {
		print "@exec","system-control","preset",two ".socket";
		print "@exec","system-control","preset","--prefix","cyclog@",two ".socket";
		print "@exec","system-control","reset",two ".socket";
		print "@exec","system-control","reset","cyclog@" two ".socket";
		print "@unexec","system-control","disable",two ".socket";
		print "@unexec","system-control","disable","cyclog@" two ".socket";
		print "@unexec","system-control","unload-when-stopped",two ".socket";
		print "@unexec","system-control","unload-when-stopped","cyclog@" two ".socket";
		print "@unexec","system-control","stop",two ".socket";
		print "@unexec","system-control","stop","cyclog@" two ".socket";
	} else
	if ("timer_with_dedicated_logger" == $1) {
		print "@exec","system-control","preset",two ".socket";
		print "@exec","system-control","preset","--prefix","cyclog@",two ".socket";
		print "@exec","system-control","reset",two ".socket";
		print "@exec","system-control","reset","cyclog@" two ".socket";
		print "@unexec","system-control","disable",two ".socket";
		print "@unexec","system-control","disable","cyclog@" two ".socket";
		print "@unexec","system-control","unload-when-stopped",two ".socket";
		print "@unexec","system-control","unload-when-stopped","cyclog@" two ".socket";
		print "@unexec","system-control","stop",two ".socket";
		print "@unexec","system-control","stop","cyclog@" two ".socket";
	} else
	if ("fan_in_logger" == $1) {
		print "@exec","system-control","preset",two "-log.service";
		print "@exec","system-control","reset",two "-log.service";
		print "@unexec","system-control","disable",two "-log.service";
		print "@unexec","system-control","unload-when-stopped",two "-log.service";
		print "@unexec","system-control","stop",two "-log.service";
	} else
	if ("service_only" == $1) {
		print "@exec","system-control","preset",two ".service";
		print "@exec","system-control","reset",two ".service";
		print "@unexec","system-control","disable",two ".service";
		print "@unexec","system-control","unload-when-stopped",two ".service";
		print "@unexec","system-control","stop",two ".service";
	} else
	if ("service_only_no_run" == $1) {
		print "@exec","system-control","preset",two ".service";
		print "@unexec","system-control","disable",two ".service";
		print "@unexec","system-control","unload-when-stopped",two ".service";
		print "@unexec","system-control","stop",two ".service";
	} else
	if ("target" == $1) {
		print "@exec","system-control","preset",two ".target";
		print "@unexec","system-control","disable",two ".target";
	} else
	if ("user_tty" == $1) {
		;
	} else
	if ("directory" == $1) {
		;
	} else
		;
}
