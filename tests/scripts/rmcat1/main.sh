#!/bin/bash
programname=$0

function usage {
    echo "usage: $programname [-r|-rtpprofile num]"
    echo "	-r --rtprofile		determines the rtp testing profile"
    echo "				equal to the ./server --profile=profile_num"
    echo "      -p --period             determines the period of the report generation"
    exit 1
}
 
if [[ $# < 2 ]]
then
  usage
fi


RPROFILE=73
REPPERIOD=5

while [[ $# > 1 ]]
do
key="$1"
case $key in
    -r|--rtpprofile)
    RPROFILE="$2"
    shift # past argument
    ;;
    -p|--period)
    REPPERIOD="$2"
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


echo ".-------------------------------------------------------------."
echo "| Test starts with the following parameters                   |"
echo "| RTP profile:   "$RPROFILE
echo "| Report period: "$REPPERIOD
echo "'-------------------------------------------------------------'"


NSSND="ns_snd"
NSRCV="ns_rcv"
SERVER="./server"
CLIENT="./client"
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
  DURATION=120
  
  #setup virtual ethernet interface controller script
  echo "./scripts/veth_ctrler.sh --veth 0 --output $LOGSDIR/veth0.csv --input $TESTDIR/veth0.csv --roothandler 1 --leafhandler 2" > scripts/test_bw_veth0_snd.sh
  chmod 777 scripts/test_bw_veth0_snd.sh

  #start client and server
  sudo ip netns exec $NSRCV $CLIENT "--profile="$RPROFILE 2> $LOGSDIR"/"client.log &
  sleep 1
  sudo ip netns exec $NSSND $SERVER "--profile="$RPROFILE 2> $LOGSDIR"/"server.log &

  sleep 1
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
  pkill client
  pkill server
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

cleanup


