{
	two=$2;
	gsub("^\"|\"$","",two);
	pwescaped=two
	gsub("_","_x5f",pwescaped);
	gsub("@","_x40",pwescaped);
	gsub(":","_x3a",pwescaped);
	gsub("\\+","_x2b",pwescaped);
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
		print "@newuser","ttylogin_x40" pwescaped "-log::::::/usr/bin/false";
		print "@owner","ttylogin_x40" pwescaped "-log";
		print "@mode","0755";
		print "@dir","var/log/sv/ttylogin@" two;
	} else
	if ("ktty_login_service_with_dedicated_logger" == $1) {
		print "@newuser","ttylogin_x40ttyC" pwescaped "-log::::::/usr/bin/false";
		print "@owner","ttylogin_x40ttyC" pwescaped "-log";
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
	if ("target" == $1) {
		;
	} else
	if ("user_tty" == $1) {
		;
	} else
		;
}
