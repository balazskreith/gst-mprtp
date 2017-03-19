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

echo "./bcex $CONFDIR/mprtp1.cmds " > $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_1.csv 4 1000 200 2500 200 600 400 1000 400" >> $LOGSDIR"/ntrt.sh"
echo "./bwcsv $LOGSDIR/pathbw_2.csv 4 1000 400 2500 200 600 400 1000 200" >> $LOGSDIR"/ntrt.sh"

chmod 777 $LOGSDIR"/ntrt.sh"

cleanup()
{
  pkill snd_pipeline
  pkill rcv_pipeline
  pkill bcex
  pkill bwcsv
  pkill mprtp1.sh
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

#------POST PROCESSING--------

#Making split csv file into several one based on conditions
#----------------------------------------------------------
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_packets_1.csv subflow_id 1
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_packets_2.csv subflow_id 2
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_rtp_packets.csv payload_type 96

./logsplitter $LOGSDIR/snd_packets_1.csv $LOGSDIR/snd_rtp_packets_1.csv payload_type 96
./logsplitter $LOGSDIR/snd_packets_2.csv $LOGSDIR/snd_rtp_packets_2.csv payload_type 96

./logsplitter $LOGSDIR/rcv_packets.csv $LOGSDIR/rcv_packets_1.csv subflow_id 1
./logsplitter $LOGSDIR/rcv_packets.csv $LOGSDIR/rcv_packets_2.csv subflow_id 2

./logsplitter $LOGSDIR/ply_packets.csv $LOGSDIR/ply_packets_1.csv subflow_id 1
./logsplitter $LOGSDIR/ply_packets.csv $LOGSDIR/ply_packets_2.csv subflow_id 2

#Making plotstats
#----------------------------------------------------------
./statmaker $LOGSDIR/sr_1.csv sr $LOGSDIR/snd_packets_1.csv
paste -d, $LOGSDIR/pathbw_1.csv $LOGSDIR/sr_1.csv  > $LOGSDIR/plotstat_1.csv

./statmaker $LOGSDIR/sr_2.csv sr $LOGSDIR/snd_packets_2.csv
paste -d, $LOGSDIR/pathbw_2.csv $LOGSDIR/sr_2.csv  > $LOGSDIR/plotstat_2.csv  

./statmaker $LOGSDIR/sr_ratios.csv ratio $LOGSDIR/snd_rtp_packets.csv 


#Making datstat
#----------------------------------------------------------
#goodput avg, loss rate, number of lost frames, psnr
./statmaker $LOGSDIR/gp_1.csv gp_avg $LOGSDIR/ply_packets_1.csv
./statmaker $LOGSDIR/lr_1.csv lr $LOGSDIR/rcv_packets_1.csv 

