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
DURATION=120
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

echo "./bcex $CONFDIR/rmcat1.cmds " > $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw.csv 4 1000 200 2500 200 600 200 1000 400" >> $LOGSDIR"/ntrt.sh"

chmod 777 $LOGSDIR"/ntrt.sh"

#Lets Rock
touch $SYNCTOUCHFILE

sudo ip netns exec ns_snd $LOGSDIR"/ntrt.sh" 

#------POST PROCESSING--------

#Making split csv file into several one based on conditions
#----------------------------------------------------------
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_rtp_packets.csv payload_type 96


#Making plotstats
#----------------------------------------------------------
./statmaker $LOGSDIR/sr.csv sr $LOGSDIR/snd_packets.csv
paste -d, $LOGSDIR/pathbw.csv $LOGSDIR/sr.csv  > $LOGSDIR/plotstat.csv

#Making datstat
#----------------------------------------------------------
#goodput avg, loss rate, number of lost frames, psnr
./statmaker $LOGSDIR/gp.csv gp_avg $LOGSDIR/ply_packets.csv
./statmaker $LOGSDIR/lr.csv lr $LOGSDIR/rcv_packets.csv 


