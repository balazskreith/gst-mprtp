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
DURATION=180
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
sudo ip netns exec ns_mid tc qdisc change dev veth6 root handle 1: netem delay "$OWD_SND"ms "$JITTER"ms
sudo ip netns exec ns_mid tc qdisc change dev veth5 root handle 1: netem delay "$OWD_RCV"ms "$JITTER"ms

echo "./bcex $CONFDIR/mprtp2.cmds " > $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_1.csv 5 1000 250 2000 250 1750 250 500 250 1000 500" >> $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_2.csv 5 1000 500 2000 250 1750 250 500 250 1000 250" >> $LOGSDIR"/ntrt.sh"
echo "./$SCRIPTSDIR/runs/postproc/mprtp2.sh $alg $OWD_SND" >> $LOGSDIR"/ntrt.sh"

chmod 777 $LOGSDIR"/ntrt.sh"

cleanup()
{
  pkill snd_pipeline
  pkill rcv_pipeline
  pkill bcex
  pkill bwcsv
  pkill mprtp2.sh
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

$LOGSDIR"/ntrt.sh" 

