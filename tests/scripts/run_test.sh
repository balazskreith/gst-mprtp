#!/bin/bash
programname=$0

function usage {
    echo "usage: $programname [-r|-rtpprofile num] [-s1|-sub1profile num] [-s2|-sub2profile num] [-s3|-sub3profile num]"
    echo "	-r --rtprofile		determines the rtp testing profile"
    echo "				equal to the ./server --profile=profile_num"
    echo "      -tc --testcase          determines the overall test profile"
    echo "                              0 - constant bw for subflow 1"
    echo "	-s[X] --sub[X]profile	determines the subflow test profile"
    echo "				0 - constant, 1 - "
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

while [[ $# > 1 ]]
do
key="$1"
case $key in
    -r|--rtpprofile)
    RPROFILE="$2"
    shift # past argument
    ;;
    -s1|--sub1profile)
    S1PROFILE="$2"
    shift # past argument
    ;;
    -s2|--sub2profile)
    S2PROFILE="$2"
    shift # past argument
    ;;
    -s3|--sub3profile)
    S3PROFILE="$2"
    shift # past argument
    ;;
    -tc|--testcase)
    TESTCASE="$2"
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
echo "| RTP profile: "$RPROFILE
echo "| Testcase:    "$TESTCASE
echo "'-------------------------------------------------------------'"


BWCTRLER="run_bwctrler.sh"
NSSND="ns_snd"
NSRCV="ns_rcv"
SERVER="./server"
CLIENT="./client"
TARGET_DIR="logs"

#"[-b|--bwprofile num] [-x|--bwref num] [s|--shift seconds] [-v|--veth if_num] [-j|--jitter milliseconds] [-l|--latency milliseconds] [-o|--output filename] [-i|--ip ip address] [-h|--help]"

#Report author
REPORTAUTHORFILE=$TARGET_DIR"/author.txt"
echo "Balázs Kreith" > $REPORTAUTHORFILE

#report title
REPORTTITLEFILE=$TARGET_DIR"/title.txt"
echo "RMCAT test report" > $REPORTTITLEFILE

if [ "$TESTCASE" -eq 0 ]

then
  echo "Constant Available Capacity with Single RMCAT flow" > $REPORTTITLEFILE
  echo "executing test case 0"
  echo "./scripts/run_bwctrler.sh --bwprofile 0 --bwref 1000 --shift 2 --veth 0 --jitter 1 --latency 100 --output $TARGET_DIR/veth0.csv --ip 10.0.0.1" > scripts/test_bw_veth0_snd.sh
  chmod 777 scripts/test_bw_veth0_snd.sh
  sudo ip netns exec $NSSND ./scripts/test_bw_veth0_snd.sh &
  sleep 1
  
  sudo ip netns exec $NSSND $SERVER "--profile="$RPROFILE 2> $TARGET_DIR"/"server.log &
  sudo ip netns exec $NSRCV $CLIENT "--profile="$RPROFILE 2> $TARGET_DIR"/"client.log &


elif [ "$TESTCASE" -eq 1 ]; then

  echo "Variable Available Capacity with Single RMCAT flow" > $REPORTTITLEFILE
  echo "executing test case 1"
  echo "./scripts/run_bwctrler.sh --bwprofile 1 --bwref 1000 --shift 2 --veth 0 --jitter 1 --latency 100 --output $TARGET_DIR/veth0.csv --ip 10.0.0.1" > scripts/test_bw_veth0_snd.sh
  chmod 777 scripts/test_bw_veth0_snd.sh
  sudo ip netns exec $NSSND ./scripts/test_bw_veth0_snd.sh &
  sleep 1

  sudo ip netns exec $NSSND $SERVER "--profile="$RPROFILE 2> $TARGET_DIR"/"server.log &
  sudo ip netns exec $NSRCV $CLIENT "--profile="$RPROFILE 2> $TARGET_DIR"/"client.log &
fi




cleanup()
# example cleanup function
{
  pkill client
  pkill server
  ps -ef | grep 'run_bwctrler.sh' | grep -v grep | awk '{print $2}' | xargs kill
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Ouch! Exiting ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

sleep 1

DURATION=660
let "WAIT=$DURATION/100"
for j in `seq 1 100`;
do
  for i in `seq 1 $WAIT`;
  do
    sleep 1
  done
  echo $j"*$WAIT seconds"
  ./scripts/report_generator.sh -o reports/report.pdf -tc $TESTCASE --author $REPORTAUTHORFILE --title $REPORTTITLEFILE > $TARGET_DIR"/"reportlog.txt &
  #./run_test_evaluator.sh $PROFILE
done
sleep 1

cleanup


