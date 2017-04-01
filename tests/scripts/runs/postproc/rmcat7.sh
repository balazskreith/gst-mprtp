#!/bin/bash
programname=$0
LOGSDIR="temp"
SCRIPTSDIR="scripts"
TEST="rmcat7"

SCREAM="scream"
FRACTAL="fractal"

if [ -z "$1" ] 
then
  CC=$SCREAM
  CC=$FRACTAL
else 
  CC=$1
fi

if [ -z "$2" ] 
then
  DELAY=50
else 
  DELAY=$2
fi

PATH_DELAY=$DELAY"000"

PLTOFILE=$LOGSDIR/$TEST"_"$CC"_"$DELAY"ms.pdf"

#Make some delay in order to wait the 10s log writing
sleep 10

#Making split csv file into several one based on conditions
#----------------------------------------------------------
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_rtp_packets.csv payload_type 96
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_fec_packets.csv payload_type 126

#Making plotstats
#----------------------------------------------------------
./statmaker $LOGSDIR/sr.csv sr $LOGSDIR/snd_packets.csv
./statmaker $LOGSDIR/qmd.csv qmd $LOGSDIR/snd_packets.csv $LOGSDIR/rcv_packets.csv
./statmaker $LOGSDIR/tcprate.csv tcpstat $LOGSDIR/tcps.pcap
tar -cvzf $LOGSDIR/tcps.tgz $LOGSDIR/*.pcap
rm $LOGSDIR/tcps.pcap

paste -d, $LOGSDIR/pathbw.csv $LOGSDIR/sr.csv $LOGSDIR/qmd.csv $LOGSDIR/tcprate.csv > $LOGSDIR/plotstat.csv

gnuplot -e "statfile='temp/plotstat.csv'" \
        -e "algorithm='$CC'" \
        -e "output_file='$PLTOFILE'" \
        -e "path_delay='$PATH_DELAY'" \
        $SCRIPTSDIR/runs/plots/rmcat7.plot

#Making datstat
#----------------------------------------------------------
#goodput avg, loss rate, number of lost frames, psnr
./statmaker $LOGSDIR/gp_avg.csv gp_avg $LOGSDIR/ply_packets.csv
./statmaker $LOGSDIR/fec_avg.csv fec_avg $LOGSDIR/snd_fec_packets.csv
./statmaker $LOGSDIR/lr.csv lr $LOGSDIR/snd_rtp_packets.csv $LOGSDIR/ply_packets.csv
./statmaker $LOGSDIR/nlf.csv nlf $LOGSDIR/snd_rtp_packets.csv $LOGSDIR/ply_packets.csv
./statmaker $LOGSDIR/ffre.csv ffre $LOGSDIR/snd_fec_packets.csv $LOGSDIR/snd_rtp_packets.csv $LOGSDIR/rcv_packets.csv $LOGSDIR/ply_packets.csv 

