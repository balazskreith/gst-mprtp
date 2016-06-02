#! /bin/sh

#//http://scicompsurvivalguide.blogspot.hu/2013/09/awk-average-and-standard-deviation.html
# Bulletproofing
if [  $# -le 1 ] 
then
  echo "Usage: ./avgStd.sh <file> <column>"
  exit
fi

echo "1:$1 2:$2"

# Compute Average and Std. Dev.
avg=`awk -v var=$2 'BEGIN{count=0; avg=0; std=0} {count=count+1; avg=avg+$var} END{print avg/count}' $1`
std=`awk -v var=$2 -v av=$avg 'BEGIN{count=0; std=0} {std=std + ($var-av)*($var-av); count=count+1} END{print sqrt((std)/(count-1))}' $1`

# Print results
printf "%s,%s\n" "$avg" "$std"

