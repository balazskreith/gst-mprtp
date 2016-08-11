#!/bin/bash

for rmcat in "rmcat1" "rmcat2" "rmcat3" "rmcat4" "rmcat5" "rmcat6" "rmcat7" 
do 
  sudo rm -r $rmcat 2> /dev/null
  mkdir $rmcat; mkdir $rmcat/pdfs; mkdir $rmcat/csv;
  echo "$rmcat and its subdirs (pdfs and csv) are successfully created"
done
