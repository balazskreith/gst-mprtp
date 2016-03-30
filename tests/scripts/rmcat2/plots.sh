#!/bin/bash

function usage {
    echo "usage: $programname [options value]"
    echo "	--srcdir 	determines the directory used as source of logfiles"
    echo "	--srcdir2 	determines the directory used as source of logfiles for the second flow"
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

DURFEC=1250
DURPLAYOUT=1250
DURAUTOCORRS=1250
DURRCVTHROUGHPUTS=1250
DURSNDTHROUGHPUTS=1250
DURRTCPINTVALS=625
DURLOSTS=1250

PLOTDIR="scripts/rmcat2"
PLOTFEC="$PLOTDIR/fec.plot"
PLOTPLAYOUT="$PLOTDIR/playouts.plot"
PLOTAUTOCORRS="$PLOTDIR/owd-autocorrs.plot"
PLOTRCVTHROUGHPUTS="$PLOTDIR/rcv-throughputs.plot"
PLOTSNDTHROUGHPUTS="$PLOTDIR/snd-throughputs.plot"
PLOTRTCPINTVALS="$PLOTDIR/rtcp-intervals.plot"
PLOTLOSTS="$PLOTDIR/losts.plot"


SRCFEC="$SRCDIR/fecdec_stat.csv"
SRCPLAYOUT="$SRCDIR/streamjoiner.csv"
SRCPLAYOUT2="$SRCDIR/path_1_skews.csv"
SRCAUTOCORRS="$SRCDIR/netqanalyser_1.csv"
SRCRCVTHROUGHPUTS="$SRCDIR/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS="$SRCDIR/sub_snd_sum.csv"
SRCRTCPINTVALS="$SRCDIR/sub_1_rtcp_ints.csv"
SRCLOSTS="$SRCDIR/sub_1_stat.csv"

SRC2FEC="$SRCDIR2/fecdec_stat.csv"
SRC2PLAYOUT="$SRCDIR2/streamjoiner.csv"
SRC2PLAYOUT2="$SRCDIR2/path_1_skews.csv"
SRC2AUTOCORRS="$SRCDIR2/netqanalyser_1.csv"
SRC2RCVTHROUGHPUTS="$SRCDIR2/sub_1_rcv.csv"
SRC2SNDTHROUGHPUTS="$SRCDIR2/sub_snd_sum.csv"
SRC2RTCPINTVALS="$SRCDIR2/sub_1_rtcp_ints.csv"
SRC2LOSTS="$SRCDIR2/sub_1_stat.csv"

DSTFEC="$DSTDIR/fec.pdf"
DSTPLAYOUT="$DSTDIR/playouts.pdf"
DSTAUTOCORRS="$DSTDIR/owd-autocorrs.pdf"
DSTRCVTHROUGHPUTS="$DSTDIR/rcv-throughputs.pdf"
DSTSNDTHROUGHPUTS="$DSTDIR/snd-throughputs.pdf"
DSTRTCPINTVALS="$DSTDIR/rtcp-intervals.pdf"
DSTLOSTS="$DSTDIR/losts.pdf"

DST2FEC="$DSTDIR/fec2.pdf"
DST2PLAYOUT="$DSTDIR/playouts2.pdf"
DST2AUTOCORRS="$DSTDIR/owd-autocorrs2.pdf"
DST2RCVTHROUGHPUTS="$DSTDIR/rcv-throughputs2.pdf"
DST2SNDTHROUGHPUTS="$DSTDIR/snd-throughputs2.pdf"
DST2RTCPINTVALS="$DSTDIR/rtcp-intervals2.pdf"
DST2LOSTS="$DSTDIR/losts2.pdf"

  gnuplot -e "duration='$DURFEC'" \
          -e "output_file='$DSTFEC'" \
          -e "fec_file='$SRCFEC'" \
          "$PLOTFEC"
          
  gnuplot -e "duration='$DURPLAYOUT'" \
          -e "output_file='$DSTPLAYOUT'" \
          -e "playouts_file='$SRCPLAYOUT'" \
          -e "skew_file='$SRCPLAYOUT2'" \
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
          
#plots for the second flow
          
  gnuplot -e "duration='$DURFEC'" \
          -e "output_file='$DST2FEC'" \
          -e "fec_file='$SRC2FEC'" \
          "$PLOTFEC"
          
  gnuplot -e "duration='$DURPLAYOUT'" \
          -e "output_file='$DST2PLAYOUT'" \
          -e "playouts_file='$SRC2PLAYOUT'" \
          -e "skew_file='$SRC2PLAYOUT2'" \
          "$PLOTPLAYOUT"
          
  gnuplot -e "duration='$DURAUTOCORRS'" \
          -e "output_file='$DST2AUTOCORRS'" \
          -e "autocorr_file='$SRC2AUTOCORRS'" \
          "$PLOTAUTOCORRS"
          
  gnuplot -e "duration='$DURRCVTHROUGHPUTS'" \
          -e "output_file='$DST2RCVTHROUGHPUTS'" \
          -e "throughput_file='$SRC2RCVTHROUGHPUTS'" \
          "$PLOTRCVTHROUGHPUTS"
          
  gnuplot -e "duration='$DURSNDTHROUGHPUTS'" \
          -e "output_file='$DST2SNDTHROUGHPUTS'" \
          -e "throughput_file='$SRC2SNDTHROUGHPUTS'" \
          "$PLOTSNDTHROUGHPUTS"
          
  gnuplot -e "duration='$DURRTCPINTVALS'" \
          -e "output_file='$DST2RTCPINTVALS'" \
          -e "rtcp_file='$SRC2RTCPINTVALS'" \
          "$PLOTRTCPINTVALS"
          
  gnuplot -e "duration='$DURLOSTS'" \
          -e "output_file='$DST2LOSTS'" \
          -e "stat_file='$SRC2LOSTS'" \
          "$PLOTLOSTS"          

