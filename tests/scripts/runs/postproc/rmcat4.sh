#!/bin/bash
programname=$0
LOGSDIR="temp"
SCRIPTSDIR="scripts"
TEST="rmcat4"

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

#Make siome delay in order to wait the 10s log writing
sleep 10

#Making split csv file into several one based on conditions
#----------------------------------------------------------
./logsplitter $LOGSDIR/snd_packets_1.csv $LOGSDIR/snd_rtp_packets_1.csv payload_type 96
./logsplitter $LOGSDIR/snd_packets_1.csv $LOGSDIR/snd_fec_packets_1.csv payload_type 126

./logsplitter $LOGSDIR/snd_packets_2.csv $LOGSDIR/snd_rtp_packets_2.csv payload_type 96
./logsplitter $LOGSDIR/snd_packets_2.csv $LOGSDIR/snd_fec_packets_2.csv payload_type 126

./logsplitter $LOGSDIR/snd_packets_3.csv $LOGSDIR/snd_rtp_packets_3.csv payload_type 96
./logsplitter $LOGSDIR/snd_packets_3.csv $LOGSDIR/snd_fec_packets_3.csv payload_type 126

#Making plotstats
#----------------------------------------------------------
./statmaker $LOGSDIR/sr_1.csv sr $LOGSDIR/snd_packets_1.csv
./statmaker $LOGSDIR/qmd_1.csv qmd $LOGSDIR/snd_packets_1.csv $LOGSDIR/rcv_packets_1.csv

./statmaker $LOGSDIR/sr_2.csv sr $LOGSDIR/snd_packets_2.csv
./statmaker $LOGSDIR/qmd_2.csv qmd $LOGSDIR/snd_packets_2.csv $LOGSDIR/rcv_packets_2.csv

./statmaker $LOGSDIR/sr_3.csv sr $LOGSDIR/snd_packets_3.csv
./statmaker $LOGSDIR/qmd_3.csv qmd $LOGSDIR/snd_packets_3.csv $LOGSDIR/rcv_packets_3.csv

paste -d, $LOGSDIR/pathbw.csv $LOGSDIR/sr_1.csv $LOGSDIR/qmd_1.csv $LOGSDIR/sr_2.csv $LOGSDIR/qmd_2.csv $LOGSDIR/sr_3.csv $LOGSDIR/qmd_3.csv > $LOGSDIR/plotstat.csv

gnuplot -e "statfile='temp/plotstat.csv'" \
        -e "algorithm='$CC'" \
        -e "output_file='$PLTOFILE'" \
        -e "path_delay='$PATH_DELAY'" \
        $SCRIPTSDIR/runs/plots/rmcat4.plot

#Making datstat
#----------------------------------------------------------
#goodput avg, loss rate, number of lost frames, psnr
./statmaker $LOGSDIR/gp_avg_1.csv gp_avg $LOGSDIR/ply_packets_1.csv
./statmaker $LOGSDIR/fec_avg_1.csv fec_avg $LOGSDIR/snd_fec_packets_1.csv
./statmaker $LOGSDIR/lr_1.csv lr $LOGSDIR/snd_rtp_packets_1.csv $LOGSDIR/ply_packets_1.csv
./statmaker $LOGSDIR/nlf_1.csv nlf $LOGSDIR/snd_rtp_packets_1.csv $LOGSDIR/ply_packets_1.csv
./statmaker $LOGSDIR/ffre_1.csv ffre $LOGSDIR/snd_fec_packets_1.csv $LOGSDIR/snd_rtp_packets_1.csv $LOGSDIR/rcv_packets_1.csv $LOGSDIR/ply_packets_1.csv 
awk '{sum+=$1; ++n} END { print sum/(n) }' $LOGSDIR/qmd_1.csv > $LOGSDIR/qmd_avg_1.csv

./statmaker $LOGSDIR/gp_avg_2.csv gp_avg $LOGSDIR/ply_packets_2.csv
./statmaker $LOGSDIR/fec_avg_2.csv fec_avg $LOGSDIR/snd_fec_packets_2.csv
./statmaker $LOGSDIR/lr_2.csv lr $LOGSDIR/snd_rtp_packets_2.csv $LOGSDIR/ply_packets_2.csv
./statmaker $LOGSDIR/nlf_2.csv nlf $LOGSDIR/snd_rtp_packets_2.csv $LOGSDIR/ply_packets_2.csv
./statmaker $LOGSDIR/ffre_2.csv ffre $LOGSDIR/snd_fec_packets_2.csv $LOGSDIR/snd_rtp_packets_2.csv $LOGSDIR/rcv_packets_2.csv $LOGSDIR/ply_packets_2.csv 
awk '{sum+=$1; ++n} END { print sum/(n) }' $LOGSDIR/qmd_2.csv > $LOGSDIR/qmd_avg_2.csv

./statmaker $LOGSDIR/gp_avg_3.csv gp_avg $LOGSDIR/ply_packets_3.csv
./statmaker $LOGSDIR/fec_avg_3.csv fec_avg $LOGSDIR/snd_fec_packets_3.csv
./statmaker $LOGSDIR/lr_3.csv lr $LOGSDIR/snd_rtp_packets_3.csv $LOGSDIR/ply_packets_3.csv
./statmaker $LOGSDIR/nlf_3.csv nlf $LOGSDIR/snd_rtp_packets_3.csv $LOGSDIR/ply_packets_3.csv
./statmaker $LOGSDIR/ffre_3.csv ffre $LOGSDIR/snd_fec_packets_3.csv $LOGSDIR/snd_rtp_packets_3.csv $LOGSDIR/rcv_packets_3.csv $LOGSDIR/ply_packets_3.csv 
awk '{sum+=$1; ++n} END { print sum/(n) }' $LOGSDIR/qmd_3.csv > $LOGSDIR/qmd_avg_3.csv

