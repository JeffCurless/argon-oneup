#! /bin/sh
#
for file in $(ls -1 *_config.txt)
do 
    echo "Processig file: $file"
    while read line
    do
	echo $line
   done < $file
done
