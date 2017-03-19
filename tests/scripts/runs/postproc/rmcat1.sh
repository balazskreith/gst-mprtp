#!/bin/bash
programname=$0
LOGSDIR="temp"
SCRIPTSDIR="scripts"

SCREAM="scream"
FRACTAL="fractal"

if [ -z "$1" ] 
then
  CC=$SCREAM
  CC=$FRACTAL
else 
  CC=$1
fi


#Making split csv file into several one based on conditions
#----------------------------------------------------------
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_rtp_packets.csv payload_type 96

#Making plotstats
#----------------------------------------------------------
./statmaker $LOGSDIR/sr.csv sr $LOGSDIR/snd_packets.csv
./statmaker $LOGSDIR/qmd.csv qmd $LOGSDIR/snd_packets.csv $LOGSDIR/rcv_packets.csv
paste -d, $LOGSDIR/pathbw.csv $LOGSDIR/sr.csv $LOGSDIR/qmd.csv > $LOGSDIR/plotstat.csv

gnuplot -e "statfile='temp/plotstat.csv'" \
        -e "algorithm='fractal'" \
        -e "output_file='temp/rmcat1_fractal_50ms.pdf'" \
        -e "path_delay='50000'" \
        $SCRIPTSDIR/runs/plots/rmcat1.plot

#Making datstat
#----------------------------------------------------------
#goodput avg, loss rate, number of lost frames, psnr
./statmaker $LOGSDIR/gp.csv gp_avg $LOGSDIR/ply_packets.csv
#./statmaker $LOGSDIR/tr.csv lr $LOGSDIR/rcv_packets.csv 

