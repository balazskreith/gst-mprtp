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
REPORTSDIR="reports"
REPORTEXFILE="report.tex"
STATFILE="stats.csv"
REPORTPDF="report.pdf"

SCRIPTSDIR="scripts"
TESTDIR="$SCRIPTSDIR/mprtp2"

rm -R $LOGSDIR
mkdir $LOGSDIR

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
  DURATION=170
  OWD1=100
  OWD2=100

  log_bw 40 1000 $LOGSDIR/veth0.csv
  log_bw 20 2800 $LOGSDIR/veth0.csv
  log_bw 20 600 $LOGSDIR/veth0.csv
  log_bw 20 2000 $LOGSDIR/veth0.csv
  log_bw 20 2000 $LOGSDIR/veth0.csv
  log_bw 40 1000 $LOGSDIR/veth0.csv

  log_bw 80 2000 $LOGSDIR/veth2.csv
  log_bw 20 1000 $LOGSDIR/veth2.csv
  log_bw 20 1500 $LOGSDIR/veth2.csv
  log_bw 20 1000 $LOGSDIR/veth2.csv
  log_bw 20 2000 $LOGSDIR/veth2.csv

  PEER1_SND="$SCRIPTSDIR/sender_1.sh"
  echo "tc qdisc change dev veth0 root handle 1: netem delay "$OWD1"ms" > $PEER1_SND
  echo "tc qdisc change dev veth2 root handle 1: netem delay "$OWD2"ms" > $PEER1_SND
  echo -n "./$SENDER" >> $PEER1_SND
  ./$TESTDIR/peer1params.sh >> $PEER1_SND
  chmod 777 $PEER1_SND

  PEER1_RCV="$SCRIPTSDIR/receiver_1.sh"
  echo "tc qdisc change dev veth1 root handle 1: netem delay "$OWD1"ms" > $PEER1_RCV
  echo "tc qdisc change dev veth3 root handle 1: netem delay "$OWD2"ms" > $PEER1_RCV
  echo -n "./$RECEIVER" >> $PEER1_RCV
  ./$TESTDIR/peer1params.sh >> $PEER1_RCV
  chmod 777 $PEER1_RCV
  
  #start receiver and sender
  sudo ip netns exec $NSRCV ./$PEER1_RCV 2> $LOGSDIR"/"receiver.log &
  sleep 2
  sudo ip netns exec $NSSND ./$PEER1_SND 2> $LOGSDIR"/"sender.log &

  echo "
  rm tcpstat.csv
  while true; do 
    paste -d , $LOGSDIR/snd_1_ratestat.csv $LOGSDIR/snd_2_ratestat.csv $LOGSDIR/veth0.csv $LOGSDIR/veth2.csv $LOGSDIR/streamsplitter.csv > $LOGSDIR/snd_rates.csv
    ./$TESTDIR/plots.sh --srcdir $LOGSDIR --dstdir $REPORTSDIR
    sleep $REPPERIOD
  done

  " > scripts/auto_rep_generator.sh

  chmod 777 scripts/auto_rep_generator.sh


cleanup()
# example cleanup function
{
  pkill receiver
  pkill sender
  pkill iperf
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


