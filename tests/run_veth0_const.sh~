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

BW=1000
tc qdisc change dev veth0 parent 1:12 netem loss 20%
tc class change dev veth0 parent 1:1 classid 1:12 htb rate "$BW"Kbit ceil "$BW"Kbit 
tc qdisc change dev veth0 parent 1:12 netem delay "$LATENCY"ms "$JITTER"ms
for j in `seq 1 900`;
do
  echo $BW"000,"
  sleep 1
done
#10 minute

#tc qdisc change dev veth0 parent 1:12 netem delay 100ms 0ms
