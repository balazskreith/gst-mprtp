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
TESTDIR="$SCRIPTSDIR/rmcat1"

rm -R $LOGSDIR
mkdir $LOGSDIR

#Report author
REPORTAUTHORFILE=$LOGSDIR"/author.txt"
echo "Balázs Kreith" > $REPORTAUTHORFILE

  #setup duration
  DURATION=106.3
  
  #setup virtual ethernet interface controller script
  echo "./$SCRIPTSDIR/veth_ctrler.sh --veth 0 --output $LOGSDIR/veth0.csv --input $TESTDIR/veth0.csv --roothandler 1 --leafhandler 2" > scripts/test_bw_veth0_snd.sh
  chmod 777 $SCRIPTSDIR/test_bw_veth0_snd.sh
  
  PEER1_SND="$SCRIPTSDIR/sender_1.sh"
  echo -n "./$SENDER" > $PEER1_SND
  ./$TESTDIR/peer1params.sh >> $PEER1_SND
  chmod 777 $PEER1_SND

  PEER1_RCV="$SCRIPTSDIR/receiver_1.sh"
  echo -n "./$RECEIVER" > $PEER1_RCV
  ./$TESTDIR/peer1params.sh >> $PEER1_RCV
  chmod 777 $PEER1_RCV


  #start receiver and sender
  sudo ip netns exec $NSRCV ./$PEER1_RCV 2> $LOGSDIR"/"receiver.log &
#  sleep 5
  sudo ip netns exec $NSSND ./$PEER1_SND 2> $LOGSDIR"/"sender.log &

  sleep 2
  #run a virtual ethernet interface controller script
  sudo ip netns exec $NSSND ./scripts/test_bw_veth0_snd.sh &

  echo "
  while true; do 
    ./$TESTDIR/plots.sh --srcdir $LOGSDIR --dstdir $REPORTSDIR
    ./$TESTDIR/stats.sh --srcdir $LOGSDIR --dst $REPORTSDIR/$STATFILE
    mv $LOGSDIR/ccparams_1.log $REPORTSDIR/ccparams_1.log
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


