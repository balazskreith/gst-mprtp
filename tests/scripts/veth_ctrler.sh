#!/bin/bash
#set -x
programname=$0

function usage {
    echo "usage: $programname [-v|--veth if_num] [-o|--output filename] [-h|--help] [-rh|--roothandler num] [-lh|--leafhandler] [-in|--input]"
    echo "	-v --veth	  determines the veth interface num the bandwidth changes applied for"
    echo "	-o --output	  determines the output csv file contains a bandwidth capacity in 100ms resolution"
    echo "	-h --help	  print this"
    echo "      -rh --roothandler root handler num"
    echo "      -lh --leafhandler leaf handler num"
    echo "      -in --input       input file contains the space separated data in the following order: LATENCY JITTER BW BURST LIMIT WAIT"
    exit 1
}

ROOTHANDLER=1
LEAFHANDLER=2
OUTPUT="output.csv"
VETH="veth0"
INPUT="empty"

while [[ $# > 0 ]]
do
key="$1"
case $key in
    -v|--veth)
    VETH="veth"$2
    shift # past argument
    ;;
    -o|--output)
    OUTPUT="$2"
    shift # past argument
    ;;
    -rh|--roothandler)
    ROOTHANDLER="$2"
    shift # past argument
    ;;
    -lh|--leafhandler)
    LEAFHANDLER="$2"
    shift # past argument
    ;;
    -in|--input)
    INPUT="$2"
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
    
cleanup()
# example cleanup function
{
  echo "veth_"$VETH"_cleanup"
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Exiting from $VETH ctrler ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT



function wait_and_log() {
        let "W=$1*10"
        for j in `seq 1 $W`;
	do
          DATINMS="$(date +%s%N | cut -b1-11)"
#          date +%s%N | cut -b1-11 >> $OUTPUT
#	  echo -n "$DATINMS,$BW" >> $OUTPUT
	  echo -n "$BW" >> $OUTPUT
          echo "" >> $OUTPUT
#	  sleep 0.05
	done
}

echo -n "" > $OUTPUT
while read LATENCY JITTER BW BURST LIMIT WAIT
do 
  echo "Setup $VETH for $WAIT seconds with "$LATENCY"ms delay "$JITTER"ms jitter "$BW"kbit capcity, "$BURST" burst and "$LIMIT" byte limit"
#  tc qdisc change dev "$VETH" root handle $ROOTHANDLER: netem delay "$LATENCY"ms "$JITTER"ms rate "$BW"kbit
#  ipfw pipe 1 config bw "$BW"kbit/s
#  tc qdisc change dev "$VETH" parent $ROOTHANDLER: handle $LEAFHANDLER: tbf rate "$BW"kbit burst "$BURST" limit $LIMIT

#  tc qdisc change dev "$VETH" root handle $ROOTHANDLER: netem delay "$LATENCY"ms "$JITTER"ms 
#  tc qdisc change dev "$VETH" parent $ROOTHANDLER: handle $LEAFHANDLER: tbf rate "$BW"kbit ceil "$BW"kbit bfifo limit $LIMIT
#  tc class change dev "$VETH" parent $ROOTHANDLER: classid $LEAFHANDLER: htb rate 20kbps ceil 20kbps 

#  tc qdisc change dev "$VETH" root handle $ROOTHANDLER: netem delay "$LATENCY"ms
  tc qdisc change dev "$VETH" parent $ROOTHANDLER: handle $LEAFHANDLER: tbf rate "$BW"kbit burst "$BURST" latency 300ms minburst 1540

  wait_and_log $WAIT
  sleep $WAIT
done < $INPUT


cleanup
