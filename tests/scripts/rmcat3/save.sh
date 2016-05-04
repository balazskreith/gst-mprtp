#!/bin/bash

function usage {
    echo "usage: $programname [options value]"
    echo "	--logsdir 	determines the directory of the logfiles"
    echo "	--repsdir 	determines the directory of the report pdfs"
    echo "	--savdir 	determines the directory of the destination"
    echo "	--savnam 	determines the name of the saving files"
    exit 1
}


if [  $# -le 1 ] 
then 
  usage
  exit 1
fi 

LOGSDIR="logs"
REPSDIR="reports"
SAVDIR=""
SAVNAM=""
DEBUG="0"

while [[ $# > 1 ]]
do
key="$1"
case $key in
    --debug)
    DEBUG="$2"
    shift # past argument
    ;;
    --logsdir)
    LOGSDIR="$2"
    shift # past argument
    ;;
    --repsdir)
    REPSDIR="$2"
    shift # past argument
    ;;
    --savdir)
    SAVDIR="$2"
    shift # past argument
    ;;
    --savnam)
    SAVNAM="$2"
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

if [ $DEBUG = "1" ]; then
echo "debug is 1"
  set -x
fi


tar cvf "$LOGSDIR/$SAVNAM.logs.tar" $LOGSDIR

if [ ! -d "$SAVDIR" ]; then
  mkdir $SAVDIR
fi

mv "$LOGSDIR/$SAVNAM.logs.tar" $SAVDIR
mv "$REPSDIR/report.pdf" "$SAVDIR/$SAVNAM.pdf"
