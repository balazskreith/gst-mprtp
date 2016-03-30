#!/bin/bash

function usage {
    echo "usage: $programname [options value]"
    echo "	--srcdir 	determines the directory used as source of pdf plotfiles"
    echo "	--srcdir2 	determines the directory used as source of logfiles for the second flow"    
    echo "	--dst    	determines the path for the output"
    exit 1
}


if [  $# -le 1 ] 
then 
  usage
  exit 1
fi 

DST="stats.csv"
SRCDIR2="logs2"
SRCDIR="logs"

while [[ $# > 1 ]]
do
key="$1"

case $key in
    --srcdir)
    SRCDIR="$2"
    shift # past argument
    ;;
    --srcdir2)
    SRCDIR2="$2"
    shift # past argument
    ;;        
    --dst)
    DST="$2"
    shift # past argument
    ;;
    --default)
    DEFAULT=YES
    ;;
    *)
    # unknown option
    ;;
esac
shift # past argument or value
done

SCRIPTSDIR="scripts"
TEMPFILE="$SRCDIR/stat_temp.csv"

printf "" > $DST

#goodput
./$SCRIPTSDIR/avgStd.sh $SRCDIR/sub_1_stat.csv 1 >> $DST
#fraction losts
./$SCRIPTSDIR/avgStd.sh $SRCDIR/sub_1_stat.csv 2 >> $DST
#lost packets
./$SCRIPTSDIR/avgStd.sh $SRCDIR/sub_1_stat.csv 3 >> $DST

#goodput
./$SCRIPTSDIR/avgStd.sh $SRCDIR2/sub_1_stat.csv 1 >> $DST
#fraction losts
./$SCRIPTSDIR/avgStd.sh $SRCDIR2/sub_1_stat.csv 2 >> $DST
#lost packets
./$SCRIPTSDIR/avgStd.sh $SRCDIR2/sub_1_stat.csv 3 >> $DST



