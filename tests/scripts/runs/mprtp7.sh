#!/bin/bash
programname=$0

NSSND="ns_snd"
NSRCV="ns_rcv"
LOGSDIR="temp"
SCRIPTSDIR="scripts"
CONFDIR=$SCRIPTSDIR"/configs"
TEMPDIR=$SCRIPTSDIR"/temp"
SYNCTOUCHFILE="triggered_stat"

mkdir $LOGSDIR

#setup defaults
DURATION=350
OWD_RCV=100
JITTER=0

if [ -z "$1" ] 
then
  OWD_SND=50
else 
  OWD_SND=$1
fi

echo "The selected OWD is $OWD_SND"

sudo ip netns exec ns_mid tc qdisc change dev veth2 root handle 1: netem delay "$OWD_SND"ms "$JITTER"ms
sudo ip netns exec ns_mid tc qdisc change dev veth1 root handle 1: netem delay "$OWD_RCV"ms "$JITTER"ms

echo "./bcex $CONFDIR/mprtp7.cmds " > $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_1.csv 1 2000 1200" >> $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_2.csv 1 2000 1200" >> $LOGSDIR"/ntrt.sh"
echo "./$SCRIPTSDIR/runs/postproc/mprtp7.sh $alg $OWD_SND" >> $LOGSDIR"/ntrt.sh"

echo "sudo tcpdump -n tcp -ni veth2 -s0 -w $LOGSDIR/tcps_1.pcap &" > $LOGSDIR"/tcpdump_1.sh"
chmod 777 $LOGSDIR"/tcpdump_1.sh"

echo "sudo tcpdump -n tcp -ni veth6 -s0 -w $LOGSDIR/tcps_2.pcap &" > $LOGSDIR"/tcpdump_2.sh"
chmod 777 $LOGSDIR"/tcpdump_2.sh"


chmod 777 $LOGSDIR"/ntrt.sh"

cleanup()
{
  pkill snd_pipeline
  pkill rcv_pipeline
  pkill bcex
  pkill bwcsv
  pkill mprtp7.sh
}
 
control_c()
{
  echo -en "\n*** Program is terminated ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

#Lets Rock
touch $SYNCTOUCHFILE

sudo ip netns exec ns_mid $LOGSDIR"/tcpdump_1.sh"
sudo ip netns exec ns_mid $LOGSDIR"/tcpdump_2.sh"
$LOGSDIR"/ntrt.sh" 

