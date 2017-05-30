#!/bin/bash
programname=$0
LOGSDIR="temp"
SCRIPTSDIR="scripts"
TEST="mprtp1"

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
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_packets_s1.csv subflow_id 1
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_packets_s2.csv subflow_id 2
./logsplitter $LOGSDIR/snd_packets.csv $LOGSDIR/snd_rtp_packets.csv payload_type 96

./logsplitter $LOGSDIR/snd_packets_s1.csv $LOGSDIR/snd_rtp_packets_s1.csv payload_type 96
./logsplitter $LOGSDIR/snd_packets_s2.csv $LOGSDIR/snd_rtp_packets_s2.csv payload_type 96

./logsplitter $LOGSDIR/snd_packets_s1.csv $LOGSDIR/snd_fec_packets_s1.csv payload_type 126
./logsplitter $LOGSDIR/snd_packets_s2.csv $LOGSDIR/snd_fec_packets_s2.csv payload_type 126

./logsplitter $LOGSDIR/rcv_packets.csv $LOGSDIR/rcv_packets_s1.csv subflow_id 1
./logsplitter $LOGSDIR/rcv_packets.csv $LOGSDIR/rcv_packets_s2.csv subflow_id 2

./logsplitter $LOGSDIR/ply_packets.csv $LOGSDIR/ply_packets_s1.csv subflow_id 1
./logsplitter $LOGSDIR/ply_packets.csv $LOGSDIR/ply_packets_s2.csv subflow_id 2


#Making plotstats
#----------------------------------------------------------
./statmaker $LOGSDIR/sr_s1.csv sr $LOGSDIR/snd_packets_s1.csv
./statmaker $LOGSDIR/qmd_s1.csv qmd $LOGSDIR/snd_packets_s1.csv $LOGSDIR/rcv_packets_s1.csv
./statmaker $LOGSDIR/qmd_s2.csv qmd $LOGSDIR/snd_packets_s2.csv $LOGSDIR/rcv_packets_s2.csv

./statmaker $LOGSDIR/sr_s2.csv sr $LOGSDIR/snd_packets_s2.csv

./statmaker $LOGSDIR/sr_ratios.csv ratio $LOGSDIR/snd_rtp_packets.csv

paste -d, $LOGSDIR/pathbw_1.csv $LOGSDIR/sr_s1.csv $LOGSDIR/qmd_s1.csv \
  $LOGSDIR/pathbw_2.csv $LOGSDIR/sr_s2.csv $LOGSDIR/qmd_s2.csv > $LOGSDIR/plotstat.csv 

gnuplot -e "statfile='temp/plotstat.csv'" \
        -e "output_file='$PLTOFILE_SUBFLOWS'" \
        -e "path_delay='$PATH_DELAY'" \
        $SCRIPTSDIR/runs/plots/mprtp1_subflows.plot
        
gnuplot -e "statfile='temp/plotstat.csv'" \
        -e "output_file='$PLTOFILE_AGGR'" \
        -e "path_delay='$PATH_DELAY'" \
        $SCRIPTSDIR/runs/plots/mprtp1_aggr.plot

#Making datstat
#----------------------------------------------------------
#goodput avg, loss rate, number of lost frames, psnr
./statmaker $LOGSDIR/gp_avg.csv gp_avg $LOGSDIR/ply_packets_s1.csv
./statmaker $LOGSDIR/fec_avg.csv fec_avg $LOGSDIR/snd_fec_packets_s1.csv
./statmaker $LOGSDIR/lr.csv lr $LOGSDIR/snd_rtp_packets_s1.csv $LOGSDIR/ply_packets_s1.csv
./statmaker $LOGSDIR/nlf.csv nlf $LOGSDIR/snd_rtp_packets_s2.csv $LOGSDIR/ply_packets_s2.csv
./statmaker $LOGSDIR/ffre.csv ffre $LOGSDIR/snd_fec_packets_s1.csv $LOGSDIR/snd_rtp_packets_s1.csv $LOGSDIR/rcv_packets_s1.csv $LOGSDIR/ply_packets_s1.csv 
awk '{sum+=$1; ++n} END { print sum/(n) }' $LOGSDIR/qmd_s1.csv > $LOGSDIR/qmd_s1_avg.csv
awk '{sum+=$1; ++n} END { print sum/(n) }' $LOGSDIR/qmd_s2.csv > $LOGSDIR/qmd_s2_avg.csv

