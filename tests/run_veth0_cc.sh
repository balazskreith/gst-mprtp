#! /bin/bash
#Test3 - Playout test
#echo "Single congestion test on veth0"
#echo "S1:100ms - 2ms"
#To attach a TBF with a sustained maximum rate of 1mbit/s, 
#a peakrate of 2.0mbit/s, 
#a 10kilobyte buffer, with a pre-bucket queue size limit calculated so the TBF causes at most 70ms of latency, 
#with perfect peakrate behavior, enter:
#tc qdisc add dev veth0 root tbf rate 1mbit burst 10kb latency 70ms peakrate 2mbit minburst 1540
#tc qdisc change dev veth0 root tbf rate 1mbit burst 10kb latency 70ms peakrate 2mbit minburst 1540

LATENCY=100
JITTER=1
MAXBW=1000

while [[ $# > 1 ]]
do
key="$1"

case $key in
    -x|--maxbw)
    MAXBW="$2"
    shift # past argument
    ;;
    --default)
    DEFAULT=YES
    ;;
    *)
            # unknown option
    ;;
esac
shift # past argument or value
done

MINBW=500
BWDIFF=500
let "MINBW = $MAXBW / 2"
let "BWDIFF = MAXBW - MINBW"

#echo "Setup veth0 to 1000KBit"
BW=$MAXBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 60`;
do
  echo $BW"000,"
  sleep 1
done
#1 minute

#4 stair closing
for i in `seq 1 4`;
do
  let BW=MAXBW-BWDIFF/4*$i
  tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
  tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
  for j in `seq 1 15`;
  do
    echo $BW"000,"
    sleep 1
  done
done 
#2 minute

#constant bw
BW=$MINBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 60`;
do
  echo $BW"000,"
  sleep 1
done
#3 minute

#echo "8 Stairs opening"
for i in `seq 1 8`;
do
  let BW=MINBW+BWDIFF/8*$i
  tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
  tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
  for j in `seq 1 7`;
  do
    echo $BW"000,"
    sleep 1
  done
done 
#4 minute

#constant bw
BW=$MAXBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 60`;
do
  echo $BW"000,"
  sleep 1
done
#5 minute

#echo "8 Stairs closing"
for i in `seq 1 8`;
do
  let BW=MAXBW-BWDIFF/8*$i
  tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
  tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
  for j in `seq 1 7`;
  do
    echo $BW"000,"
    sleep 1
  done
done 
#6 minute

#constant bw
BW=$MINBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 60`;
do
  echo $BW"000,"
  sleep 1
done
#7 minute

#4 stair opening
for i in `seq 1 4`;
do
  let BW=MINBW+BWDIFF/4*$i
  tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
  tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
  for j in `seq 1 15`;
  do
    echo $BW"000,"
    sleep 1
  done
done 
#8 minute

#constant bw
BW=$MINBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 60`;
do
  echo $BW"000,"
  sleep 1
done
#8 minute

#constant bw
BW=$MAXBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 60`;
do
  echo $BW"000,"
  sleep 1
done
#9 minute

#constant bw
BW=$MINBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 60`;
do
  echo $BW"000,"
  sleep 1
done
#10 minute

#constant bw
BW=$MAXBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 60`;
do
  echo $BW"000,"
  sleep 1
done
#11 minute

#constant bw
BW=$MAXBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 30`;
do
  echo $BW"000,"
  sleep 1
done
#11.5 minute

#echo "8 Stairs closing"
for i in `seq 1 8`;
do
  let BW=MAXBW-BWDIFF/8*$i
  tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
  tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
  for j in `seq 1 7`;
  do
    echo $BW"000,"
    sleep 1
  done
done 
#12.5 minute

BW=$MINBW
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 30`;
do
  echo $BW"000,"
  sleep 1
done
#13 minute

#echo "8 Stairs opening"
for i in `seq 1 8`;
do
  let BW=MINBW+BWDIFF/8*$i
  tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit
  tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
  for j in `seq 1 7`;
  do
    echo $BW"000,"
    sleep 1
  done
done 
#15 minute

for j in `seq 1 4`;
do
  echo $BW"000,"
  sleep 1
done


