#!/bin/bash
programname=$0
LOGSDIR="temp"
SCRIPTSDIR="scripts"
TEST="rmcat1"

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


#Making split csv file into several one based on conditions
#----------------------------------------------------------
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_rtp_packets.csv payload_type 96

#Making plotstats
#----------------------------------------------------------
./statmaker $LOGSDIR/sr_1.csv sr $LOGSDIR/snd_packets_1.csv
./statmaker $LOGSDIR/qmd_1.csv qmd $LOGSDIR/snd_packets_1.csv $LOGSDIR/rcv_packets_1.csv
./statmaker $LOGSDIR/sr_2.csv sr $LOGSDIR/snd_packets_2.csv
./statmaker $LOGSDIR/qmd_2.csv qmd $LOGSDIR/snd_packets_2.csv $LOGSDIR/rcv_packets_2.csv
paste -d, $LOGSDIR/pathbw.csv $LOGSDIR/sr_1.csv $LOGSDIR/qmd_1.csv $LOGSDIR/sr_2.csv $LOGSDIR/qmd_2.csv > $LOGSDIR/plotstat.csv

gnuplot -e "statfile='temp/plotstat.csv'" \
        -e "algorithm='$CC'" \
        -e "output_file='$PLTOFILE'" \
        -e "path_delay='$PATH_DELAY'" \
        $SCRIPTSDIR/runs/plots/rmcat2.plot

#Making datstat
#----------------------------------------------------------
#goodput avg, loss rate, number of lost frames, psnr
./statmaker $LOGSDIR/gp.csv gp_avg $LOGSDIR/ply_packets.csv
#./statmaker $LOGSDIR/tr.csv lr $LOGSDIR/rcv_packets.csv 

