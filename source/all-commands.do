#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

command1_lists="../package/commands1 ../package/extra-manpages1"
command8_lists="../package/commands8 ../package/extra-manpages8"

(

echo getty getty-noreset
echo ${command1_lists} ${command8_lists}

cat ${command1_lists} | 
while read -r i
do 
	echo "$i.1" "$i.html"
done
cat ${command8_lists} | 
while read -r i
do 
	echo "$i.8" "$i.html"
done

cat ../package/commands1 ../package/commands8

) | 
xargs -r redo-ifchange
