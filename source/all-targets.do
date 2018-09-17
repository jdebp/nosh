#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

target_lists="../package/standard-targets"

(

echo ${target_lists}

cat ${target_lists} |
while read -r i
do
	echo targets/"$i"
done 

) | 
xargs -r redo-ifchange
