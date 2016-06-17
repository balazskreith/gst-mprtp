#!/bin/bash
programname=$0

function usage {
    echo "usage: $programname [-p|--period]"
    echo "  -p --period         determines the period of the report generation"
    echo "  --savnam		the name of the saving"
    echo "  --savdir		the directory of the saving"
    exit 1
}
 

REPPERIOD=5
SAVDIR="0"
SAVNAM="0"

while [[ $# > 1 ]]
do
key="$1"
case $key in
    -p|--period)
    REPPERIOD="$2"
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
    ;;
    *)
            # unknown option
    ;;
esac
shift # past argument or value
done

NSSND="ns_snd"
NSRCV="ns_rcv"
SENDER="sender"
RECEIVER="receiver"
LOGSDIR="logs"
LOGSDIR2="logs2"
REPORTSDIR="reports"
REPORTEXFILE="report.tex"
STATFILE="stats.csv"
REPORTPDF="report.pdf"

SCRIPTSDIR="scripts"
TESTDIR="$SCRIPTSDIR/rmcat3"

rm -R $LOGSDIR
mkdir $LOGSDIR
rm -R $LOGSDIR2
mkdir $LOGSDIR2

#Report author
REPORTAUTHORFILE=$LOGSDIR"/author.txt"
echo "Balázs Kreith" > $REPORTAUTHORFILE

function log_bw() {
        let "W=$1*10"
        for j in `seq 1 $W`;
	do
	  echo -n "$2" >> $3
          echo "" >> $3
	done
}


  #setup duration
  DURATION=120.0
  OWD_SND=300
  OWD_RCV=100

  sudo ip netns exec $NSSND tc qdisc change dev veth0 parent 1: handle 2: tbf rate 2000kbit burst 15400 latency 300ms minburst 1540
#  sudo ip netns exec $NSRCV tc qdisc change dev veth1 parent 1: handle 2: tbf rate 2000kbit burst 15400 latency 300ms minburst 1540

  log_bw 20 2000 $LOGSDIR/veth0.csv
  log_bw 20 1000 $LOGSDIR/veth0.csv
  log_bw 20 500 $LOGSDIR/veth0.csv
  log_bw 40 2000 $LOGSDIR/veth0.csv

  log_bw 35 2000 $LOGSDIR2/veth1.csv
  log_bw 35 800 $LOGSDIR2/veth1.csv
  log_bw 30 2000 $LOGSDIR2/veth1.csv

  PEER1_SND="$SCRIPTSDIR/sender_1.sh"
  echo "tc qdisc change dev veth0 root handle 1: netem delay "$OWD_SND"ms" > $PEER1_SND
  echo -n "./$SENDER" >> $PEER1_SND
  ./$TESTDIR/peer1params.sh >> $PEER1_SND
  chmod 777 $PEER1_SND

  PEER1_RCV="$SCRIPTSDIR/receiver_1.sh"
  echo "tc qdisc change dev veth1 root handle 1: netem delay "$OWD_RCV"ms" > $PEER1_RCV
  echo -n "./$RECEIVER" > $PEER1_RCV
  ./$TESTDIR/peer1params.sh >> $PEER1_RCV
  chmod 777 $PEER1_RCV

  PEER2_SND="$SCRIPTSDIR/sender_2.sh"
  echo -n "./$SENDER" > $PEER2_SND
  ./$TESTDIR/peer2params.sh >> $PEER2_SND
  chmod 777 $PEER2_SND

  PEER2_RCV="$SCRIPTSDIR/receiver_2.sh"
  echo -n "./$RECEIVER" > $PEER2_RCV
  ./$TESTDIR/peer2params.sh >> $PEER2_RCV
  chmod 777 $PEER2_RCV

  rm $LOGSDIR"/signal.log"

  #start receiver and sender
  sudo ip netns exec $NSRCV ./$PEER1_RCV 2> $LOGSDIR"/"receiver.log &
  sudo ip netns exec $NSSND ./$PEER2_RCV 2> $LOGSDIR"/"receiver2.log &
  sleep 2
  sudo ip netns exec $NSSND ./$PEER1_SND 2> $LOGSDIR"/"sender.log &
  sudo ip netns exec $NSRCV ./$PEER2_SND 2> $LOGSDIR"/"sender2.log &

  echo "
  while true; do 
    ./$TESTDIR/plots.sh --srcdir $LOGSDIR --dstdir $REPORTSDIR
    #./$TESTDIR/report.sh --srcdir $REPORTSDIR --author $REPORTAUTHORFILE --dst $REPORTEXFILE
    #./$SCRIPTSDIR/pdflatex.sh $REPORTEXFILE

    #mv $REPORTPDF $REPORTSDIR/$REPORTPDF
    sleep $REPPERIOD
  done

  " > scripts/auto_rep_generator.sh

  chmod 777 scripts/auto_rep_generator.sh


cleanup()
# example cleanup function
{
  pkill receiver
  pkill sender
  ps -ef | grep 'veth_ctrler.sh' | grep -v grep | awk '{print $2}' | xargs kill
  ps -ef | grep 'report_generato' | grep -v grep | awk '{print $2}' | xargs kill
  ps -ef | grep 'main.sh' | grep -v grep | awk '{print $2}' | xargs kill

}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Exiting run_test.sh ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

./$SCRIPTSDIR/auto_rep_generator.sh  > report.log &

sleep $DURATION

if [ "$SAVDIR" != "0" ]
then
  echo "./$TESTDIR/save.sh --logsdir $LOGSDIR --repsdir $REPORTSDIR --savnam $SAVNAM --savdir $SAVDIR" > $SCRIPTSDIR/saving.sh
  chmod 777 $SCRIPTSDIR/saving.sh
  ./$SCRIPTSDIR/saving.sh
fi

cleanup


