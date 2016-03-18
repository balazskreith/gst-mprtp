#!/bin/bash
programname=$0

function usage {
    echo "usage: $programname [-p|-profile number] [-d|-duration seconds]"
    echo "	-p --profile		determines the rtp testing profile"
    echo "      -d --duration           determines the overall test duration"
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

PROFILE=0
DURATION=100

while [[ $# > 1 ]]
do
key="$1"
case $key in
    -p|--profile)
    PROFILE="$2"
    shift # past argument
    ;;
    -d|--duration)
    DURATION="$2"
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
echo "| Duration:    "$DURATION
echo "'-------------------------------------------------------------'"


BWCTRLER="run_bwctrler.sh"
NSSND="ns_snd"
NSRCV="ns_rcv"
SERVER="./server"
CLIENT="./client"
TARGET_DIR="logs"

cleanup()
# example cleanup function
{
  pkill client
  pkill server
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Ouch! Exiting ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

sudo ip netns exec $NSSND $SERVER "--profile="$PROFILE &
sudo ip netns exec $NSRCV $CLIENT "--profile="$PROFILE &
sleep $DURATION

cleanup


