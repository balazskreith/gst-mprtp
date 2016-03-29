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

DURFEC=1000
DURPLAYOUT=1000
DURAUTOCORRS=1000
DURRCVTHROUGHPUTS=1000
DURSNDTHROUGHPUTS=1000
DURRTCPINTVALS=500
DURLOSTS=3000

PLOTDIR="scripts/rmcat1"
PLOTFEC="$PLOTDIR/fec.plot"
PLOTPLAYOUT="$PLOTDIR/playouts.plot"
PLOTAUTOCORRS="$PLOTDIR/owd-autocorrs.plot"
PLOTRCVTHROUGHPUTS="$PLOTDIR/rcv-throughputs.plot"
PLOTSNDTHROUGHPUTS="$PLOTDIR/snd-throughputs.plot"
PLOTRTCPINTVALS="$PLOTDIR/rtcp-intervals.plot"
PLOTLOSTS="$PLOTDIR/losts.plot"


SRCFEC="$SRCDIR/fecdec_stat.csv"
SRCPLAYOUT="$SRCDIR/playouts.csv"
SRCAUTOCORRS="$SRCDIR/netqanalyser_1.csv"
SRCRCVTHROUGHPUTS="$SRCDIR/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS="$SRCDIR/sub_snd_sum.csv"
SRCRTCPINTVALS="$SRCDIR/sub_1_rtcp_ints.csv"
SRCLOSTS="$SRCDIR/sub_1_stat.csv"

DSTFEC="$DSTDIR/fec.pdf"
DSTPLAYOUT="$DSTDIR/playouts.pdf"
DSTAUTOCORRS="$DSTDIR/owd-autocorrs.pdf"
DSTRCVTHROUGHPUTS="$DSTDIR/rcv-throughputs.pdf"
DSTSNDTHROUGHPUTS="$DSTDIR/snd-throughputs.pdf"
DSTRTCPINTVALS="$DSTDIR/rtcp-intervals.pdf"
DSTLOSTS="$DSTDIR/losts.pdf"

  gnuplot -e "duration='$DURFEC'" \
          -e "output_file='$DSTFEC'" \
          -e "fec_file='$SRCFEC'" \
          "$PLOTFEC"
          
  gnuplot -e "duration='$DURPLAYOUT'" \
          -e "output_file='$DSTPLAYOUT'" \
          -e "playouts_file='$SRCPLAYOUT'" \
          "$PLOTPLAYOUT"
          
  gnuplot -e "duration='$DURAUTOCORRS'" \
          -e "output_file='$DSTAUTOCORRS'" \
          -e "autocorr_file='$SRCAUTOCORRS'" \
          "$PLOTAUTOCORRS"
          
  gnuplot -e "duration='$DURRCVTHROUGHPUTS'" \
          -e "output_file='$DSTRCVTHROUGHPUTS'" \
          -e "throughput_file='$SRCRCVTHROUGHPUTS'" \
          "$PLOTRCVTHROUGHPUTS"
          
  gnuplot -e "duration='$DURSNDTHROUGHPUTS'" \
          -e "output_file='$DSTSNDTHROUGHPUTS'" \
          -e "throughput_file='$SRCSNDTHROUGHPUTS'" \
          "$PLOTSNDTHROUGHPUTS"
          
  gnuplot -e "duration='$DURRTCPINTVALS'" \
          -e "output_file='$DSTRTCPINTVALS'" \
          -e "rtcp_file='$SRCRTCPINTVALS'" \
          "$PLOTRTCPINTVALS"
          
  gnuplot -e "duration='$DURLOSTS'" \
          -e "output_file='$DSTLOSTS'" \
          -e "stat_file='$SRCLOSTS'" \
          "$PLOTLOSTS"                  

