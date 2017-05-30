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

echo "./bcex $CONFDIR/mprtp3.cmds " > $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_forward_1.csv 4 2000 200 1000 200 500 200 2000 750" >> $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_forward_2.csv 4 2000 400 1000 200 500 200 2000 550" >> $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_backward_1.csv 3 2000 350 800 350 2000 650" >> $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_backward_2.csv 3 2000 700 800 350 2000 300" >> $LOGSDIR"/ntrt.sh"
echo "./$SCRIPTSDIR/runs/postproc/mprtp3.sh $alg $OWD_SND" >> $LOGSDIR"/ntrt.sh"

chmod 777 $LOGSDIR"/ntrt.sh"

cleanup()
{
  pkill snd_pipeline
  pkill rcv_pipeline
  pkill bcex
  pkill bwcsv
  pkill mprtp3.sh
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

