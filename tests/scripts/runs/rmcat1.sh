#!/bin/bash
programname=$0

NSSND="ns_snd"
NSRCV="ns_rcv"
LOGSDIR="temp"
SCRIPTSDIR="scripts"
TEMPDIR=$SCRIPTSDIR"/temp"
SYNCTOUCHFILE="triggered_stat"

mkdir $LOGSDIR
rm $LOGSDIR/*
rm $SYNCTOUCHFILE

#setup defaults
DURATION=120
OWD_SND=100
OWD_RCV=100


sudo ip netns exec ns_mid tc qdisc change dev veth2 root handle 1: netem delay "$OWD_SND"ms
sudo ip netns exec ns_mid tc qdisc change dev veth1 root handle 1: netem delay "$OWD_RCV"ms

echo "ntrt -c$CONFDIR/ntrt_snd_meas.ini -m$CONFDIR/ntrt_rmcat2.cmds -t120 " > $LOGSDIR"/ntrt.sh"
chmod 777 $LOGSDIR"/ntrt.sh"

sudo ip netns exec ns_snd $LOGSDIR"/ntrt.sh" 

#setup the bandwidth
sudo ip netns exec ns_mid tc qdisc change dev veth2 parent 1: handle 2: tbf rate 1000kbit burst 100kbit latency 300ms minburst 1540
sudo ip netns exec ns_mid tc qdisc change dev veth1 parent 1: handle 2: tbf rate 1000kbit burst 100kbit latency 300ms minburst 1540
echo "Environment is ready, lets rock"

#Lets Rock
touch $SYNCTOUCHFILE

#set the file contains the info for bandwidth in kbit
echo -n "" > $LOGSDIR/bandwidth.csv
for i in `seq 1 400`;
do
	echo "1000" >> $LOGSDIR/bandwidth.csv
done 
echo "Path 1 in forward direction capacity changed to 1000kbit/s"
sleep 40

sudo ip netns exec ns_mid tc qdisc change dev veth2 parent 1: handle 2: tbf rate 2800kbit burst 100kbit latency 300ms minburst 15400
for i in `seq 1 200`;
do
	echo "2800" >> $LOGSDIR/bandwidth.csv
done 
echo "Path 1 in forward direction capacity changed to 2800kbit/s"
sleep 20

sudo ip netns exec ns_mid tc qdisc change dev veth2 parent 1: handle 2: tbf rate 600kbit burst 100kbit latency 300ms minburst 1540
for i in `seq 1 200`;
do
	echo "600" >> $LOGSDIR/bandwidth.csv
done 
echo "Path 1 in forward direction capacity changed to 600kbit/s"
sleep 20

sudo ip netns exec ns_mid tc qdisc change dev veth2 parent 1: handle 2: tbf rate 1000kbit burst 100kbit latency 300ms minburst 1540
for i in `seq 1 200`;
do
	echo "1000" >> $LOGSDIR/bandwidth.csv
done 
echo "Path 1 in forward direction capacity changed to 1000kbit/s"
sleep 20
