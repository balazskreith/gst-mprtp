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

DURFEC=3000
DURRCVQUEUE=3000
DURRCVTHROUGHPUTS=3000
DURSNDTHROUGHPUTS=3000
DURRTCPINTVALS=3000
DURLOSTS=3000

PLOTDIR="scripts/rmcat7"
PLOTFEC="$PLOTDIR/fec.plot"
PLOTRCVQUEUE="$PLOTDIR/rcvqueue.plot"
PLOTAUTOCORRS="$PLOTDIR/owd-autocorrs.plot"
PLOTRCVTHROUGHPUTS="$PLOTDIR/rcv-throughputs.plot"
PLOTSNDTHROUGHPUTS="$PLOTDIR/snd-throughputs.plot"
PLOTRTCPINTVALS="$PLOTDIR/rtcp-intervals.plot"

SRCFEC="$SRCDIR/fecdec_stat.csv"
SRCRCVQUEUE="$SRCDIR/packetsrcvqueue.csv"
SRCAUTOCORRS="$SRCDIR/rmdiautocorrs_1.csv"
SRCRCVTHROUGHPUTS="$SRCDIR/sub_1_rcv.csv"
SRCSNDTHROUGHPUTS="$SRCDIR/snd_1_ratestat.csv"
SRCFECRATES="$SRCDIR/fecrates.csv"
SRCRTCPINTVALS="$SRCDIR/sub_1_rtcp_ints.csv"

SRCVETH0="$SRCDIR/veth0.csv"
#SRCVETH02="$SRCDIR/veth02.csv"
#SRCSNDTHROUGHPUTS2="$SRCDIR/snd_sum_ratestat.csv"
#cat $SRCSNDTHROUGHPUTS | tail -1000 > $SRCSNDTHROUGHPUTS2
#sort -u -t',' -k1,1 "$SRCVETH0" > $SRCVETH02
#join -t , -1 6 -2 1 "$SRCSNDTHROUGHPUTS" "$SRCVETH02" > "$SRCSNDTHROUGHPUTS2"

DSTFEC="$DSTDIR/fec.pdf"
DSTRCVQUEUE="$DSTDIR/rcvqueue.pdf"
DSTAUTOCORRS="$DSTDIR/owd-autocorrs.pdf"
DSTRCVTHROUGHPUTS="$DSTDIR/rcv-throughputs.pdf"
DSTSNDTHROUGHPUTS="$DSTDIR/snd-throughputs.pdf"
DSTRTCPINTVALS="$DSTDIR/rtcp-intervals.pdf"

  gnuplot -e "duration='$DURFEC'" \
          -e "output_file='$DSTFEC'" \
          -e "fec_file='$SRCFEC'" \
          "$PLOTFEC"
          
  gnuplot -e "duration='$DURRCVTHROUGHPUTS'" \
          -e "output_file='$DSTRCVTHROUGHPUTS'" \
          -e "throughput_file='$SRCRCVTHROUGHPUTS'" \
          "$PLOTRCVTHROUGHPUTS"
          
  gnuplot -e "duration='$DURSNDTHROUGHPUTS'" \
          -e "output_file='$DSTSNDTHROUGHPUTS'" \
          -e "throughput_file='$SRCSNDTHROUGHPUTS'" \
          -e "bw_file='$SRCVETH0'" \
	  -e "fecrates_file='$SRCFECRATES'" \
          "$PLOTSNDTHROUGHPUTS"

#          -e "throughput_file='$SRCSNDTHROUGHPUTS'" \
          
  gnuplot -e "duration='$DURRTCPINTVALS'" \
          -e "output_file='$DSTRTCPINTVALS'" \
          -e "rtcp_file='$SRCRTCPINTVALS'" \
          "$PLOTRTCPINTVALS"

