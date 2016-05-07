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
LOGSDIR3="logs3"
LOGSDIR4="logs4"
LOGSDIR5="logs5"

REPORTSDIR="reports"
REPORTEXFILE="report.tex"
STATFILE="stats.csv"
REPORTPDF="report.pdf"

SCRIPTSDIR="scripts"
TESTDIR="$SCRIPTSDIR/rmcat5"

rm -R $LOGSDIR
mkdir $LOGSDIR
rm -R $LOGSDIR2
mkdir $LOGSDIR2
rm -R $LOGSDIR3
mkdir $LOGSDIR3
rm -R $LOGSDIR4
mkdir $LOGSDIR4
rm -R $LOGSDIR5
mkdir $LOGSDIR5

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
  DURATION=320
  
  log_bw 300 3500 $LOGSDIR/veth0.csv

  PEER1_SND="$SCRIPTSDIR/sender_1.sh"
  echo -n "./$SENDER" > $PEER1_SND
  ./$TESTDIR/peer1params.sh >> $PEER1_SND
  chmod 777 $PEER1_SND

  PEER2_SND="$SCRIPTSDIR/sender_2.sh"
  echo "sleep 10" > $PEER2_SND
  echo -n "./$SENDER" >> $PEER2_SND
  ./$TESTDIR/peer2params.sh >> $PEER2_SND
  chmod 777 $PEER2_SND

  PEER3_SND="$SCRIPTSDIR/sender_3.sh"
  echo "sleep 20" > $PEER3_SND
  echo -n "./$SENDER" >> $PEER3_SND
  ./$TESTDIR/peer3params.sh >> $PEER3_SND
  chmod 777 $PEER3_SND

  PEER4_SND="$SCRIPTSDIR/sender_4.sh"
  echo "sleep 40" > $PEER4_SND
  echo -n "./$SENDER" >> $PEER4_SND
  ./$TESTDIR/peer4params.sh >> $PEER4_SND
  chmod 777 $PEER4_SND

  PEER5_SND="$SCRIPTSDIR/sender_5.sh"
  echo "sleep 50" > $PEER5_SND
  echo -n "./$SENDER" >> $PEER5_SND
  ./$TESTDIR/peer5params.sh >> $PEER5_SND
  chmod 777 $PEER5_SND



  PEER1_RCV="$SCRIPTSDIR/receiver_1.sh"
  echo -n "./$RECEIVER" > $PEER1_RCV
  ./$TESTDIR/peer1params.sh >> $PEER1_RCV
  chmod 777 $PEER1_RCV

  PEER2_RCV="$SCRIPTSDIR/receiver_2.sh"
  echo "sleep 20" > $PEER2_RCV
  echo -n "./$RECEIVER" >> $PEER2_RCV
  ./$TESTDIR/peer2params.sh >> $PEER2_RCV
  chmod 777 $PEER2_RCV

  PEER3_RCV="$SCRIPTSDIR/receiver_3.sh"
  echo "sleep 40" > $PEER3_RCV
  echo -n "./$RECEIVER" >> $PEER3_RCV
  ./$TESTDIR/peer3params.sh >> $PEER3_RCV
  chmod 777 $PEER3_RCV

  PEER4_RCV="$SCRIPTSDIR/receiver_4.sh"
  echo "sleep 40" > $PEER4_RCV
  echo -n "./$RECEIVER" >> $PEER4_RCV
  ./$TESTDIR/peer4params.sh >> $PEER4_RCV
  chmod 777 $PEER4_RCV

  PEER5_RCV="$SCRIPTSDIR/receiver_5.sh"
  echo "sleep 50" > $PEER5_RCV
  echo -n "./$RECEIVER" >> $PEER5_RCV
  ./$TESTDIR/peer5params.sh >> $PEER5_RCV
  chmod 777 $PEER5_RCV

  rm $LOGSDIR"/signal.log"
  
  echo "tc qdisc change dev veth0 parent 1: handle 2: tbf rate 3500kbit burst 15400 latency 300ms minburst 1540" > snd_cmd.sh
  echo "tc qdisc change dev veth0 root handle 2: netem delay 10ms" >> snd_cmd.sh
  echo "tc qdisc change dev veth2 root handle 2: netem delay 25ms" >> snd_cmd.sh
  echo "tc qdisc change dev veth4 root handle 3: netem delay 50ms" >> snd_cmd.sh
  echo "tc qdisc change dev veth6 root handle 4: netem delay 100ms" >> snd_cmd.sh
  echo "tc qdisc change dev veth8 root handle 5: netem delay 150ms" >> snd_cmd.sh

  echo "tc qdisc change dev veth1 parent 1: handle 2: tbf rate 3500kbit burst 15400 latency 300ms minburst 1540" > rcv_cmd.sh

  chmod 777 snd_cmd.sh
  chmod 777 rcv_cmd.sh
  sudo ip netns exec $NSSND ./snd_cmd.sh
  sudo ip netns exec $NSRCV ./rcv_cmd.sh

  #start receiver and sender
  sudo ip netns exec $NSRCV ./$PEER1_RCV 2> $LOGSDIR"/"receiver.log &
  sudo ip netns exec $NSRCV ./$PEER2_RCV 2> $LOGSDIR2"/"receiver2.log &
  sudo ip netns exec $NSRCV ./$PEER3_RCV 2> $LOGSDIR3"/"receiver3.log &
  sudo ip netns exec $NSRCV ./$PEER4_RCV 2> $LOGSDIR4"/"receiver3.log &
  sudo ip netns exec $NSRCV ./$PEER4_RCV 2> $LOGSDIR5"/"receiver3.log &
  sleep 3
  sudo ip netns exec $NSSND ./$PEER1_SND 2> $LOGSDIR"/"sender.log &
  sudo ip netns exec $NSSND ./$PEER2_SND 2> $LOGSDIR2"/"sender.log &
  sudo ip netns exec $NSSND ./$PEER3_SND 2> $LOGSDIR3"/"sender.log &
  sudo ip netns exec $NSSND ./$PEER4_SND 2> $LOGSDIR4"/"sender.log &
  sudo ip netns exec $NSSND ./$PEER5_SND 2> $LOGSDIR5"/"sender.log &

  echo "
  while true; do 
    ./$TESTDIR/plots.sh --srcdir $LOGSDIR --dstdir $REPORTSDIR
    ./$TESTDIR/report.sh --srcdir $REPORTSDIR --author $REPORTAUTHORFILE --dst $REPORTEXFILE
    ./$SCRIPTSDIR/pdflatex.sh $REPORTEXFILE

    mv $REPORTPDF $REPORTSDIR/$REPORTPDF
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


