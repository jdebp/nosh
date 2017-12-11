{
	two=$2;
	gsub("^\"|\"$","",two);
	pwescaped=two
	gsub("_","_m",pwescaped);
	gsub("@","_a",pwescaped);
	gsub(":","_c",pwescaped);
	gsub(";","_h",pwescaped);
	gsub(",","_v",pwescaped);
	gsub("\\.","_d",pwescaped);
	gsub("\\+","_p",pwescaped);
	if ("log_user" == $1) {
		print "@newuser",pwescaped "-log::::::/usr/bin/false";
	} else
	if ("non_log_user" == $1) {
		print "@newuser",pwescaped "::::::/usr/bin/false";
	} else
	if ("service_with_dedicated_logger" == $1) {
		print "@newuser",pwescaped "-log::::::/usr/bin/false";
		print "@owner",pwescaped "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/" two;
	} else
	if ("login_service_with_dedicated_logger" == $1) {
		print "@newuser","ttylogin_a" pwescaped "-log::::::/usr/bin/false";
		print "@owner","ttylogin_a" pwescaped "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/ttylogin@" two;
	} else
	if ("ktty_login_service_with_dedicated_logger" == $1) {
		print "@newuser","ttylogin_attyC" pwescaped "-log::::::/usr/bin/false";
		print "@owner","ttylogin_attyC" pwescaped "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/ttylogin@ttyC" two;
	} else
	if ("socket_with_dedicated_logger" == $1) {
		print "@newuser",pwescaped "-log::::::/usr/bin/false";
		print "@owner",pwescaped "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/" two;
	} else
	if ("fan_in_logger" == $1) {
		print "@newuser",pwescaped "-log::::::/usr/bin/false";
		print "@owner",pwescaped "-log";
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
		;
}
