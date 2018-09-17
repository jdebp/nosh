{
	two=$2;
	gsub("^\"|\"$","",two);
	three=$3;
	gsub("^\"|\"$","",three);
	four=$4;
	gsub("^\"|\"$","",four);
	pwescaped2=two
	gsub("_","_m",pwescaped2);
	gsub("@","_a",pwescaped2);
	gsub(":","_c",pwescaped2);
	gsub(";","_h",pwescaped2);
	gsub(",","_v",pwescaped2);
	gsub("\\.","_d",pwescaped2);
	gsub("\\+","_p",pwescaped2);
	pwescaped3=two
	gsub("_","_m",pwescaped3);
	gsub("@","_a",pwescaped3);
	gsub(":","_c",pwescaped3);
	gsub(";","_h",pwescaped3);
	gsub(",","_v",pwescaped3);
	gsub("\\.","_d",pwescaped3);
	gsub("\\+","_p",pwescaped3);
	if ("logfile_owning_user" == $1) {
		print "@newuser",pwescaped2 "-log::::::/usr/bin/true";
	} else
	if ("file_owning_user" == $1) {
		print "@newuser",pwescaped2 ":::::" pwescaped3 ":/usr/bin/true";
	} else
	if ("non_file_owning_user" == $1) {
		print "@newuser",pwescaped2 "::::::/usr/bin/true";
	} else
	if ("user_vt_user" == $1) {
		print "@newuser","user-vt-" pwescaped2 "::::::/usr/bin/true";
	} else
	if ("user_vt_group" == $1) {
		print "@newgroup","user-vt-" pwescaped2;
	} else
	if ("service_with_dedicated_logger" == $1) {
		print "@newuser",pwescaped2 "-log::::::/usr/bin/true";
		print "@owner",pwescaped2 "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/" two;
	} else
	if ("login_service_with_dedicated_logger" == $1) {
		print "@newuser","ttylogin_a" pwescaped2 "-log::::::/usr/bin/true";
		print "@owner","ttylogin_a" pwescaped2 "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/ttylogin@" two;
	} else
	if ("ktty_login_service_with_dedicated_logger" == $1) {
		print "@newuser","ttylogin_attyC" pwescaped2 "-log::::::/usr/bin/true";
		print "@owner","ttylogin_attyC" pwescaped2 "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/ttylogin@ttyC" two;
	} else
	if ("socket_with_dedicated_logger" == $1) {
		print "@newuser",pwescaped2 "-log::::::/usr/bin/true";
		print "@owner",pwescaped2 "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/" two;
	} else
	if ("timer_with_dedicated_logger" == $1) {
		print "@newuser",pwescaped2 "-log::::::/usr/bin/true";
		print "@owner",pwescaped2 "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/" two;
	} else
	if ("fan_in_logger" == $1) {
		print "@newuser",pwescaped2 "-log::::::/usr/bin/true";
		print "@owner",pwescaped2 "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/" two;
	} else
	if ("service_only" == $1) {
		;
	} else
	if ("service_only_no_run" == $1) {
		;
	} else
	if ("target" == $1) {
		;
	} else
	if ("user_tty" == $1) {
		;
	} else
	if ("directory" == $1) {
                gsub("^/","",three);
		print "@owner",pwescaped2;
		print "@mode",four;
		print "@dir",three;
	} else
		;
}
