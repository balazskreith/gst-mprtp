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
    --srcdir2)
    SRCDIR2="$2"
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

DURRCVTHROUGHPUTS=1000
DURSNDTHROUGHPUTS=1000
DURRTCPINTVALS=1000

PLOTDIR="scripts/rmcat3"
PLOTRCVTHROUGHPUTS="$PLOTDIR/rcv-throughputs.plot"
PLOTSNDTHROUGHPUTS="$PLOTDIR/snd-throughputs.plot"
PLOTRTCPINTVALS="$PLOTDIR/rtcp-intervals.plot"

SRCRCVTHROUGHPUTS="$SRCDIR/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS="$SRCDIR/snd_1_ratestat.csv"
SRCRTCPINTVALS="$SRCDIR/sub_1_rtcp_ints.csv"

SRCRCVTHROUGHPUTS2="$SRCDIR2/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS2="$SRCDIR2/snd_1_ratestat.csv"

SRCVETH0="$SRCDIR/veth0.csv"
SRCVETH1="$SRCDIR2/veth1.csv"

DSTRCVTHROUGHPUTS="$DSTDIR/rcv-throughputs.pdf"
DSTSNDTHROUGHPUTS="$DSTDIR/snd-throughputs.pdf"
DSTRTCPINTVALS="$DSTDIR/rtcp-intervals.pdf"

DSTRCVTHROUGHPUTS2="$DSTDIR/rcv-throughputs2.pdf"
DSTSNDTHROUGHPUTS2="$DSTDIR/snd-throughputs2.pdf"

  gnuplot -e "duration='$DURRCVTHROUGHPUTS'" \
          -e "output_file='$DSTRCVTHROUGHPUTS'" \
          -e "throughput_file='$SRCRCVTHROUGHPUTS'" \
          "$PLOTRCVTHROUGHPUTS"
          
  gnuplot -e "duration='$DURSNDTHROUGHPUTS'" \
          -e "output_file='$DSTSNDTHROUGHPUTS'" \
          -e "throughput_file='$SRCSNDTHROUGHPUTS'" \
          -e "bw_file='$SRCVETH0'" \
          "$PLOTSNDTHROUGHPUTS"

  #gnuplot -e "duration='$DURRCVTHROUGHPUTS'" \
  #        -e "output_file='$DSTRCVTHROUGHPUTS2'" \
  #        -e "throughput_file='$SRCRCVTHROUGHPUTS2'" \
  #        "$PLOTRCVTHROUGHPUTS2"
          
  #gnuplot -e "duration='$DURSNDTHROUGHPUTS'" \
  #        -e "output_file='$DSTSNDTHROUGHPUTS2'" \
  #        -e "throughput_file='$SRCSNDTHROUGHPUTS2'" \
  #        -e "bw_file='$SRCVETH1'" \
  #        "$PLOTSNDTHROUGHPUTS2"

