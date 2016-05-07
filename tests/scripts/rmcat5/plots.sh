#!/bin/bash

function usage {
    echo "usage: $programname [options value]"
    echo "	--srcdir 	determines the directory used as source of logfiles"
    echo "	--dstdir 	determines the directory used as destination of plot pdfs"
    echo "	--debug 	determines weather commands are printed out or not"
    exit 1
}


if [  $# -le 1 ] 
then 
  usage
  exit 1
fi 

SRCDIR="logs"
SRCDIR2="logs2"
SRCDIR3="logs3"
SRCDIR4="logs4"
SRCDIR5="logs5"
DSTDIR="reports"
DEBUG="0"

while [[ $# > 1 ]]
do
key="$1"
case $key in
    --debug)
    DEBUG="$2"
    shift # past argument
    ;;
    --srcdir)
    SRCDIR="$2"
    shift # past argument
    ;;
    --dstdir)
    DSTDIR="$2"
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

DURRCVTHROUGHPUTS=1200
DURSNDTHROUGHPUTS=1200

PLOTDIR="scripts/rmcat4"
PLOTRCVTHROUGHPUTS="$PLOTDIR/rcv-throughputs.plot"
PLOTSNDTHROUGHPUTS="$PLOTDIR/snd-throughputs.plot"

SRCRCVTHROUGHPUTS="$SRCDIR/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS="$SRCDIR/snd_1_ratestat.csv"

SRCRCVTHROUGHPUTS2="$SRCDIR2/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS2="$SRCDIR2/snd_1_ratestat.csv"

SRCRCVTHROUGHPUTS3="$SRCDIR3/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS3="$SRCDIR3/snd_1_ratestat.csv"

SRCRCVTHROUGHPUTS4="$SRCDIR4/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS4="$SRCDIR4/snd_1_ratestat.csv"

SRCRCVTHROUGHPUTS5="$SRCDIR5/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS5="$SRCDIR5/snd_1_ratestat.csv"

SRCVETH0="$SRCDIR/veth0.csv"

DSTRCVTHROUGHPUTS="$DSTDIR/rcv-throughputs.pdf"
DSTSNDTHROUGHPUTS="$DSTDIR/snd-throughputs.pdf"

          
  gnuplot -e "duration='$DURRCVTHROUGHPUTS'" \
          -e "output_file='$DSTRCVTHROUGHPUTS'" \
          -e "throughput_file='$SRCRCVTHROUGHPUTS'" \
          -e "throughput_file2='$SRCRCVTHROUGHPUTS2'" \
          -e "throughput_file3='$SRCRCVTHROUGHPUTS3'" \
          -e "throughput_file4='$SRCRCVTHROUGHPUTS4'" \
          -e "throughput_file5='$SRCRCVTHROUGHPUTS5'" \
          "$PLOTRCVTHROUGHPUTS"
          


