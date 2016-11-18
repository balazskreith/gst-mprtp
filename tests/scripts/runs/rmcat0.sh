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

#setup the bandwidth
sudo ip netns exec ns_mid tc qdisc change dev veth2 parent 1: handle 2: tbf rate 1000kbit burst 100kbit latency 300ms minburst 1540
sudo ip netns exec ns_mid tc qdisc change dev veth1 parent 1: handle 2: tbf rate 1000kbit burst 100kbit latency 300ms minburst 1540

#Lets Rock
touch $SYNCTOUCHFILE

#set the file contains the info for bandwidth in kbit
echo -n "" > $LOGSDIR/bandwidth.csv
for i in `seq 1 1000`;
do
	echo "1000" >> $LOGSDIR/bandwidth.csv
done 

echo "Environment is ready, lets sleep for $DURATION seconds"
sleep $DURATION



