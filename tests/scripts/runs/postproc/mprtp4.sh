#!/bin/bash
programname=$0
LOGSDIR="temp"
SCRIPTSDIR="scripts"
TEST="mprtp4"

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

PLTOFILE_SUBFLOWS=$LOGSDIR/$TEST"_"$CC"_"$DELAY"ms_subflows.pdf"
PLTOFILE_AGGR=$LOGSDIR/$TEST"_"$CC"_"$DELAY"ms_aggr.pdf"

#Make siome delay in order to wait the 10s log writing
sleep 10

#Making split csv file into several one based on conditions
#----------------------------------------------------------
./logsplitter $LOGSDIR/snd_packets_1.csv $LOGSDIR/snd_packets_1_s1.csv subflow_id 1
./logsplitter $LOGSDIR/snd_packets_1.csv $LOGSDIR/snd_packets_1_s2.csv subflow_id 2
./logsplitter $LOGSDIR/snd_packets_1.csv $LOGSDIR/snd_rtp_packets_1.csv payload_type 96

./logsplitter $LOGSDIR/snd_packets_1_s1.csv $LOGSDIR/snd_rtp_packets_1_s1.csv payload_type 96
./logsplitter $LOGSDIR/snd_packets_1_s2.csv $LOGSDIR/snd_rtp_packets_1_s2.csv payload_type 96

./logsplitter $LOGSDIR/snd_packets_1_s1.csv $LOGSDIR/snd_fec_packets_1_s1.csv payload_type 126
./logsplitter $LOGSDIR/snd_packets_1_s2.csv $LOGSDIR/snd_fec_packets_1_s2.csv payload_type 126

./logsplitter $LOGSDIR/rcv_packets_1.csv $LOGSDIR/rcv_packets_1_s1.csv subflow_id 1
./logsplitter $LOGSDIR/rcv_packets_1.csv $LOGSDIR/rcv_packets_1_s2.csv subflow_id 2

./logsplitter $LOGSDIR/ply_packets_1.csv $LOGSDIR/ply_packets_1_s1.csv subflow_id 1
./logsplitter $LOGSDIR/ply_packets_1.csv $LOGSDIR/ply_packets_1_s2.csv subflow_id 2


./logsplitter $LOGSDIR/snd_packets_2.csv $LOGSDIR/snd_packets_2_s1.csv subflow_id 1
./logsplitter $LOGSDIR/snd_packets_2.csv $LOGSDIR/snd_packets_2_s2.csv subflow_id 2
./logsplitter $LOGSDIR/snd_packets_2.csv $LOGSDIR/snd_rtp_packets_2.csv payload_type 96

./logsplitter $LOGSDIR/snd_packets_2_s1.csv $LOGSDIR/snd_rtp_packets_2_s1.csv payload_type 96
./logsplitter $LOGSDIR/snd_packets_2_s2.csv $LOGSDIR/snd_rtp_packets_2_s2.csv payload_type 96

./logsplitter $LOGSDIR/snd_packets_2_s1.csv $LOGSDIR/snd_fec_packets_2_s1.csv payload_type 126
./logsplitter $LOGSDIR/snd_packets_2_s2.csv $LOGSDIR/snd_fec_packets_2_s2.csv payload_type 126

./logsplitter $LOGSDIR/rcv_packets_2.csv $LOGSDIR/rcv_packets_2_s1.csv subflow_id 1
./logsplitter $LOGSDIR/rcv_packets_2.csv $LOGSDIR/rcv_packets_2_s2.csv subflow_id 2

./logsplitter $LOGSDIR/ply_packets_2.csv $LOGSDIR/ply_packets_2_s1.csv subflow_id 1
./logsplitter $LOGSDIR/ply_packets_2.csv $LOGSDIR/ply_packets_2_s2.csv subflow_id 2


#Making plotstats
#----------------------------------------------------------
./statmaker $LOGSDIR/sr_1_s1.csv sr $LOGSDIR/snd_packets_1_s1.csv
./statmaker $LOGSDIR/sr_1_s2.csv sr $LOGSDIR/snd_packets_1_s2.csv
./statmaker $LOGSDIR/qmd_1_s1.csv qmd $LOGSDIR/snd_packets_1_s1.csv $LOGSDIR/rcv_packets_1_s1.csv
./statmaker $LOGSDIR/qmd_1_s2.csv qmd $LOGSDIR/snd_packets_1_s2.csv $LOGSDIR/rcv_packets_1_s2.csv

./statmaker $LOGSDIR/sr_2_s1.csv sr $LOGSDIR/snd_packets_2_s1.csv
./statmaker $LOGSDIR/sr_2_s2.csv sr $LOGSDIR/snd_packets_2_s2.csv
./statmaker $LOGSDIR/qmd_2_s1.csv qmd $LOGSDIR/snd_packets_2_s1.csv $LOGSDIR/rcv_packets_2_s1.csv
./statmaker $LOGSDIR/qmd_2_s2.csv qmd $LOGSDIR/snd_packets_2_s2.csv $LOGSDIR/rcv_packets_2_s2.csv


set -x
paste -d, $LOGSDIR/pathbw_1.csv $LOGSDIR/sr_1_s1.csv $LOGSDIR/qmd_1_s1.csv \
  $LOGSDIR/pathbw_2.csv $LOGSDIR/sr_1_s2.csv $LOGSDIR/qmd_1_s2.csv \
  $LOGSDIR/sr_2_s1.csv $LOGSDIR/qmd_2_s1.csv \
  $LOGSDIR/sr_2_s2.csv $LOGSDIR/qmd_2_s2.csv > $LOGSDIR/plotstat.csv 

gnuplot -e "statfile='temp/plotstat.csv'" \
        -e "output_file='$PLTOFILE_SUBFLOWS'" \
        -e "path_delay='$PATH_DELAY'" \
        $SCRIPTSDIR/runs/plots/mprtp4_subflows.plot
        
gnuplot -e "statfile='temp/plotstat.csv'" \
        -e "output_file='$PLTOFILE_AGGR'" \
        -e "path_delay='$PATH_DELAY'" \
        $SCRIPTSDIR/runs/plots/mprtp4_aggr.plot

#Making datstat
#----------------------------------------------------------
#goodput avg, loss rate, number of lost frames, psnr
./statmaker $LOGSDIR/gp_avg_1_s1.csv gp_avg $LOGSDIR/ply_packets_1_s1.csv
./statmaker $LOGSDIR/fec_avg_1_s1.csv fec_avg $LOGSDIR/snd_fec_packets_1_s1.csv
./statmaker $LOGSDIR/lr_1_s1.csv lr $LOGSDIR/snd_rtp_packets_1_s1.csv $LOGSDIR/ply_packets_1_s1.csv
./statmaker $LOGSDIR/nlf_1_s1.csv nlf $LOGSDIR/snd_rtp_packets_1_s1.csv $LOGSDIR/ply_packets_1_s1.csv
./statmaker $LOGSDIR/ffre_1_s1.csv ffre $LOGSDIR/snd_fec_packets_1_s1.csv $LOGSDIR/snd_rtp_packets_1_s1.csv $LOGSDIR/rcv_packets_1_s1.csv $LOGSDIR/ply_packets_1_s1.csv 
awk '{sum+=$1; ++n} END { print sum/(n) }' $LOGSDIR/qmd_1_s1.csv > $LOGSDIR/qmd_1_s1_avg.csv
awk '{sum+=$1; ++n} END { print sum/(n) }' $LOGSDIR/qmd_1_s2.csv > $LOGSDIR/qmd_1_s2_avg.csv

