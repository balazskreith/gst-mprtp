#!/bin/bash
NSSND="ns_snd"
NSRCV="ns_snd"
programname=$0

function usage {
    echo "usage: $programname [-t|--tcpprofile num] [-n|--tcpnumber num] [-o|--output filename] [-i|--ip ip address] [-d|--duration seconds]"
    echo "	-t --tcprofile	determines the tcp testing profile"
    echo "			1 - long tcp, 2 - short tcp flows"
    echo "	-n --tcpnum	determines the number of tcp running along with the flow"
    echo "	-i --ip   	determines the IP the TCP will flow"
    echo "	-o --output	determines the csv file"
    echo "	-d --duration	determines the csv file"
    exit 1
}

if [ 2 -gt "$#"]
then
  usage
fi

IP="10.0.0.2"
TPROFILE=0
TCPFLOWS=0
DURATION=100
OUTPUT="tcp_output.csv"

while [[ $# > 1 ]]
do
key="$1"

case $key in
    -t|--tcpprofile)
    TPROFILE="$2"
    shift # past argument
    ;;
    -n|--tcpnum)
    TCPFLOWS="$2"
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


function wait_and_log() {
        let "WAIT=$1*10"
        for j in `seq 1 $WAIT`;
	do
#	  echo "$BW," >> $OUTPUT
	  sleep 0.1
	done
}

echo "iperf -s -p 45678" > scripts/iperf_snd.sh
chmod 777 scripts/iperf_snd.sh
sudo ip netns exec $NSSND ./scripts/iperf_snd.sh &


function short() {
  rand=$(perl -e 'print int(rand(90))+10'); 
  echo "iperf -c $IP -p 45678 -t $rand -P $TCPFLOWS" > scripts/iperf_rcv.sh
  chmod 777 scripts/iperf_rcv.sh
  sudo ip netns exec $NSRCV ./scripts/iperf_rcv.sh &
}


cleanup()
# example cleanup function
{
  pkill iperf
  echo "tcpflows_"$IP"_cleanup"
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** TCP FLOWS Simulation exiting ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

if [ 0 -eq "$TPROFILE"]
then
  echo "iperf -c $IP -p 45678 -t $DURATION -P $TCPFLOWS" > scripts/iperf_rcv.sh
  chmod 777 scripts/iperf_rcv.sh
  sudo ip netns exec $NSRCV ./scripts/iperf_rcv.sh
else
  for i in `seq 1 50`;
    do
    rand2=$(perl -e 'print int(rand(2))+1');
    for j in `seq 1 $rand`;
      do
      short
    done
    rand=$(perl -e 'print int(rand(90))+10');
    sleep $rand
  done
fi



