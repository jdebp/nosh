/^Z/ {
	if ("Z" root != $1) {
		print $1 ":" $2 ":" $3 ":::lo";
		print $1 ":" $2 ":" $3 ":::si";
	} else
		print "# skipped " $1 ":" $2 ":" $3
}
/^&/ {
	if ("&" root != $1) {
		print $1 ":" $2 ":" $3 ":::lo";
		print $1 ":" $2 ":" $3 ":::si";
	} else
		print "# skipped " $1 ":" $2 ":" $3
}
/^\+/ {
	print $1 ":" $2 ":::lo";
	print $1 ":" $2 ":::si";
}
