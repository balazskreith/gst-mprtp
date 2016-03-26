#!/bin/bash
programname=$0

function usage {
    echo "usage: $programname [-r|-rtpprofile num]"
    echo "	-r --rtprofile		determines the rtp testing profile"
    echo "				equal to the ./server --profile=profile_num"
    echo "      -tc --testcase          determines the test case for bandwidth controlling"
    echo "      -p --period             determines the period of the report generation"
    exit 1
}
 
if [[ $# < 2 ]]
then
  usage
fi

# Process arguments
# copyright: http://stackoverflow.com/questions/192249/how-do-i-parse-command-line-arguments-in-bash

# Use > 1 to consume two arguments per pass in the loop (e.g. each
# argument has a corresponding value to go with it).
# Use > 0 to consume one or more arguments per pass in the loop (e.g.
# some arguments don't have a corresponding value to go with it such
# as in the --default example).
# note: if this is set to > 0 the /etc/hosts part is not recognized ( may be a bug )

S1PROFILE=0
S2PROFILE=0
S3PROFILE=0
RPROFILE=73
TESTCASE=0
REPPERIOD=5

while [[ $# > 1 ]]
do
key="$1"
case $key in
    -r|--rtpprofile)
    RPROFILE="$2"
    shift # past argument
    ;;
    -tc|--testcase)
    TESTCASE="$2"
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
echo "| Testcase:      "$TESTCASE
echo "| Report period: "$REPPERIOD
echo "'-------------------------------------------------------------'"


NSSND="ns_snd"
NSRCV="ns_rcv"
SERVER="./server"
CLIENT="./client"
LOGSDIR="logs"
REPORTSDIR="reports"
REPORTEXFILE="report.tex"
REPORTPDF="report.pdf"

#Report author
REPORTAUTHORFILE=$LOGSDIR"/author.txt"
echo "Balázs Kreith" > $REPORTAUTHORFILE

if [ "$TESTCASE" -eq 0 ]

then
  #setup duration
  DURATION=350
  
  #setup virtual ethernet interface controller script
  echo "./scripts/veth_ctrler.sh --veth 0 --output $LOGSDIR/veth0.csv --input scripts/test0_veth0.csv --roothandler 1 --leafhandler 2" > scripts/test_bw_veth0_snd.sh
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
    ./scripts/plots_generator.sh --testcase $TESTCASE --srcdir $LOGSDIR --dstdir $REPORTSDIR
    mv $LOGSDIR/ccparams_1.log $REPORTSDIR/ccparams_1.log
    ./scripts/test0_report.sh --srcdir $REPORTSDIR --author $REPORTAUTHORFILE --dst $REPORTEXFILE
    ./scripts/pdflatex.sh $REPORTEXFILE
    mv $REPORTPDF $REPORTSDIR/$REPORTPDF
    sleep $REPPERIOD
  done

  " > scripts/auto_rep_generator.sh

  chmod 777 scripts/auto_rep_generator.sh

elif [ "$TESTCASE" -eq 1 ]; then

 #setup duration
  DURATION=120
  
  #setup virtual ethernet interface controller script
  echo "./scripts/veth_ctrler.sh --veth 0 --output $LOGSDIR/veth0.csv --input scripts/test1_veth0.csv --roothandler 1 --leafhandler 2" > scripts/test_bw_veth0_snd.sh
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
    ./scripts/plots_generator.sh --testcase $TESTCASE --srcdir $LOGSDIR --dstdir $REPORTSDIR
    mv $LOGSDIR/ccparams_1.log $REPORTSDIR/ccparams_1.log
    ./scripts/test1_report.sh --srcdir $REPORTSDIR --author $REPORTAUTHORFILE --dst $REPORTEXFILE
    ./scripts/pdflatex.sh $REPORTEXFILE
    mv $REPORTPDF $REPORTSDIR/$REPORTPDF
    sleep $REPPERIOD
  done

  " > scripts/auto_rep_generator.sh

  chmod 777 scripts/auto_rep_generator.sh

elif [ "$TESTCASE" -eq 2 ]; then

 #setup duration
  DURATION=120
  
  #setup virtual ethernet interface controller script
  echo "./scripts/veth_ctrler.sh --veth 0 --output $LOGSDIR/veth0.csv --input scripts/test2_veth0.csv --roothandler 1 --leafhandler 2" > scripts/test_bw_veth0_snd.sh
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
    ./scripts/plots_generator.sh --testcase $TESTCASE --srcdir $LOGSDIR --dstdir $REPORTSDIR
    mv $LOGSDIR/ccparams_1.log $REPORTSDIR/ccparams_1.log
    ./scripts/test1_report.sh --srcdir $REPORTSDIR --author $REPORTAUTHORFILE --dst $REPORTEXFILE
    ./scripts/pdflatex.sh $REPORTEXFILE
    mv $REPORTPDF $REPORTSDIR/$REPORTPDF
    sleep $REPPERIOD
  done

  " > scripts/auto_rep_generator.sh

  chmod 777 scripts/auto_rep_generator.sh
fi


mv 

cleanup()
# example cleanup function
{
  pkill client
  pkill server
  ps -ef | grep 'veth_ctrler.sh' | grep -v grep | awk '{print $2}' | xargs kill
  ps -ef | grep 'report_generato' | grep -v grep | awk '{print $2}' | xargs kill
  ps -ef | grep 'run_test' | grep -v grep | awk '{print $2}' | xargs kill

}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Exiting run_test.sh ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

./scripts/auto_rep_generator.sh  > report.log &

sleep $DURATION

cleanup


