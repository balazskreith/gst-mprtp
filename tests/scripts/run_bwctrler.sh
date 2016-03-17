#!/bin/bash
NSSND="ns_snd"
NSRCV="ns_snd"

programname=$0

function usage {
    echo "usage: $programname [-b|--bwprofile num] [-x|--bwref num] [s|--shift seconds] [-v|--veth if_num] [-j|--jitter milliseconds] [-l|--latency milliseconds] [-o|--output filename] [-i|--ip ip address] [-h|--help]"
    echo "	-b --bwprofile	determines the testing profile"
    echo "			https://tools.ietf.org/html/draft-ietf-rmcat-eval-test-02"
    echo "			0 - Constant, 1 - "
    echo "	-x --bwref	determines the reference bandwidth (in Kbit) used in the test"
    echo "	-s --shift	determines the timeshift before the bw profile is started"
    echo "	-v --veth	determines the veth interface num the bandwidth changes applied for"
    echo "	-j --jitter	determines the jitter"
    echo "	-l --latency	determines the latency"
    echo "	-i --ip   	determines the IP the TCP connections are used for connecting"
    echo "	-o --output	determines the csv file"
    echo "	-h --help	print this"
    exit 1
}


BWREF=1000
LATENCY=50
LIMIT=0
SNUM=0
BPROFILE=0
JITTER=1
OUTPUT="output.csv"

# Process arguments
# copyright: http://stackoverflow.com/questions/192249/how-do-i-parse-command-line-arguments-in-bash

# Use > 1 to consume two arguments per pass in the loop (e.g. each
# argument has a corresponding value to go with it).
# Use > 0 to consume one or more arguments per pass in the loop (e.g.
# some arguments don't have a corresponding value to go with it such
# as in the --default example).
# note: if this is set to > 0 the /etc/hosts part is not recognized ( may be a bug )
while [[ $# > 0 ]]
do
key="$1"
case $key in
    -b|--bwprofile)
    BPROFILE="$2"
    shift # past argument
    ;;
    -r|--rtpprofile)
    RPROFILE="$2"
    shift # past argument
    ;;
    -x|--bwref)
    BWREF="$2"
    shift # past argument
    ;;
    -v|--veth)
    VNUM="$2"
    shift # past argument
    ;;
    -s|--shift)
    SNUM="$2"
    shift # past argument
    ;;
    -j|--jitter)
    JITTER="$2"
    shift # past argument
    ;;
    -l|--latency)
    LATENCY="$2"
    shift # past argument
    ;;
    -o|--output)
    OUTPUT="$2"
    shift # past argument
    ;;
    -i|--ip)
    IP="$2"
    shift # past argument
    ;;
    -h|--help)
    usage
    ;;
    --default)
    ;;
    *)
            # unknown option
    ;;
esac
shift # past argument or value
done
    

VETH="veth"$VNUM

echo "" > $OUTPUT

echo "   +--------------------------------------+-------------------------------+"
echo "   | Arguments name                       | Value                         |"
echo "   +--------------------------------------+-------------------------------+"
echo "   | Bandwidth profile                    | $BPROFILE                     "
echo "   | Bandwidth max                        | $BWREF                        "
echo "   | Veth                                 | $VETH                         "
echo "   | Shift                                | $SNUM                         "
echo "   | Jitter                               | $JITTER                       "
echo "   | Latency                              | $LATENCY			"
echo "   | Output                               | $OUTPUT			"
echo "   +----------------------------------------------------------------------+"

#usage: path interface bandwidth
function path() {
	let "BW=$2"
	let "LIMIT=$2*125"
	let "LIMIT=$LIMIT/5"
	echo "Setup $1 for "$LATENCY"ms and "$BW"Kbit rate with "$LIMIT" queue limit"
	tc qdisc change dev "$1" root handle 1: netem delay "$LATENCY"ms "$JITTER"ms
	tc qdisc change dev "$1" parent 1: handle 2: tbf rate "$BW"kbit burst 100kbit limit $LIMIT
}

function wait_and_log() {
        let "WAIT=$1*10"
        for j in `seq 1 $WAIT`;
	do
	  echo "$BW," >> $OUTPUT
	  sleep 0.1
	done
}


function test0() {
	echo "   +-------------------------------------------------------------------+"
	echo "   | The selected bandwidth profile for $VETH is 0                     "
	echo "   +-------------------------------------------------------------------+"
	echo ""
	echo "   +-------------------------------------------------------------------+"
	echo "   | Expected behavior: the candidate algorithm is expected to detect  |"
	echo "   | the path capacity constraint and converges to bottleneck link's   |"
	echo "   | capacity.                                                         |"
	echo "   +-------------------------------------------------------------------+"
	echo ""
	echo "                                 Forward -->"
	echo " +---+       +-----+                               +-----+       +---+"
	echo " |S1 |=======|  A  |------------------------------>|  B  |=======|R1 |"
	echo " +---+       |     |<------------------------------|     |       +---+"
	echo "             +-----+                               +-----+"
	echo "                             <-- Backward"
	echo ""
	echo "   +--------------------+--------------+-----------+-------------------+"
	echo "   | Variation pattern  | Path         | Start     | Path capacity     |"
	echo "   | index              | direction    | time      | ratio             |"
	echo "   +--------------------+--------------+-----------+-------------------+"
	echo "   | One                | Forward      | 600s      | 1.0               |"
	echo "   +--------------------+--------------+-----------+-------------------+"
	

	let "BW=$BWREF"
        path $VETH $BW
        wait_and_log 600 
}

function test1() {
	echo "   +-------------------------------------------------------------------+"
	echo "   | The selected bandwidth profile for $VETH is 1                     "
	echo "   +-------------------------------------------------------------------+"
	echo ""
	echo "   +-------------------------------------------------------------------+"
	echo "   | Expected behavior: the candidate algorithm is expected to detect  |"
	echo "   | the path capacity constraint, converges to bottleneck link's      |"
	echo "   | capacity and adapt the flow to avoid unwanted oscillation when the|"
	echo "   | sending bit rate is approaching the bottleneck link's capacity.   |"
	echo "   | The oscillations occur when the media flow(s) attempts to reach   |"
	echo "   | its maximum bit rate, overshoots the usage of the available       |"
	echo "   | bottleneck capacity, to rectify it reduces the bit rate and       |"
	echo "   |starts to ramp up again.                                           |"
	echo "   +-------------------------------------------------------------------+"
	echo ""
	echo "                                 Forward -->"
	echo " +---+       +-----+                               +-----+       +---+"
	echo " |S1 |=======|  A  |------------------------------>|  B  |=======|R1 |"
	echo " +---+       |     |<------------------------------|     |       +---+"
	echo "             +-----+                               +-----+"
	echo "                             <-- Backward"
	echo ""
	echo "   +--------------------+--------------+-----------+-------------------+"
	echo "   | Variation pattern  | Path         | Start     | Path capacity     |"
	echo "   | index              | direction    | time      | ratio             |"
	echo "   +--------------------+--------------+-----------+-------------------+"
	echo "   | One                | Forward      | 0s        | 1.0               |"
	echo "   | Two                | Forward      | 40s       | 2.5               |"
	echo "   | Three              | Forward      | 60s       | 0.6               |"
	echo "   | Four               | Forward      | 80s       | 1.0               |"
	echo "   +--------------------+--------------+-----------+-------------------+"
	
        #./scripts/run_tcpflow.sh -t 0 -n 3 -i $IP -d &

	let "BW=$BWREF"
        path $VETH $BW
        wait_and_log 40 
	let "BW=$BWREF*5"
	let "BW=$BW/2"
        path $VETH $BW
        wait_and_log 20 
	let "BW=$BWREF*6"
	let "BW=$BW/10"
        path $VETH $BW
        wait_and_log 20 
	let "BW=$BWREF"
        path $VETH $BW
        wait_and_log 20 
	
}


cleanup()
# example cleanup function
{
  pkill iperf
  ps -ef | grep 'run_tcpflow.sh' | grep -v grep | awk '{print $2}' | xargs kill
  echo "bwctrler_"$VETH"_cleanup"
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Ouch2! Exiting ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

path $VETH $BWREF
if [ "$SNUM" -gt 0 ]
then
  echo "initial shift is applied for $SNUM seconds"
  wait_and_log $SNUM
fi

case $BPROFILE in
    0) 
    test0
    ;;
    1) 
    test1
    ;;
    --default)
    ;;
    *)
    echo "No Bandwidth profile was given the default is used"
    test0
    ;;
esac

cleanup
