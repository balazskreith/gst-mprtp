#!/bin/bash
SERVER_LOG="server.log"
CLIENT_LOG="client.log"
SUB1_BW_LOG="veth0_bw.txt"
SUB2_BW_LOG="veth2_bw.txt"
SUB3_BW_LOG="veth4_bw.txt"
SUB1_SND_LOG="sub_1_snd.csv"
SUB1_RCV_LOG="sub_1_rcv.csv"
SUB2_SND_LOG="sub_2_snd.csv"
SUB2_RCV_LOG="sub_2_rcv.csv"
SUB3_SND_LOG="sub_3_snd.csv"
SUB3_RCV_LOG="sub_3_rcv.csv"
SUB_RCV_SUM_LOG="sub_rcv_sum.csv"
SUB_SND_SUM_LOG="sub_snd_sum.csv"
SUB1_CTRLER_LOG="subratectrler_1.log"
SUB2_CTRLER_LOG="subratectrler_2.log"
SUB3_CTRLER_LOG="subratectrler_3.log"
BW_SUM='bw_sum.csv'
BW_SUM_LOG='bw_sum_res.csv'
BW_SUM_LOG_T='bw_sum_res_t.csv'

TARGET_DIR="../../Research-Scripts/gnuplot/mprtp_stat_files"
SOURCE_DIR="logs"


PROFILE=$1
let "ACTIVE_SUBFLOWS=$PROFILE&7"
let "FOREMAN=$PROFILE&8"

if [ "$ACTIVE_SUBFLOWS" -eq 7 ]
then
  paste $SOURCE_DIR"/"$BW_SUM $SOURCE_DIR"/"veth0_bw.txt $SOURCE_DIR"/"veth2_bw.txt $SOURCE_DIR"/"veth4_bw.txt > $SOURCE_DIR"/"$BW_SUM_LOG_T
elif [ "$ACTIVE_SUBFLOWS" -eq 3 ]
then
  paste $SOURCE_DIR"/"$BW_SUM $SOURCE_DIR"/"veth0_bw.txt $SOURCE_DIR"/"veth2_bw.txt > $SOURCE_DIR"/"$BW_SUM_LOG_T
else
  paste $SOURCE_DIR"/"$BW_SUM $SOURCE_DIR"/"veth0_bw.txt > $SOURCE_DIR"/"$BW_SUM_LOG_T
fi

sed -i 's/\t//g' $SOURCE_DIR"/"$BW_SUM_LOG_T
sed -i 's/,$//' $SOURCE_DIR"/"$BW_SUM_LOG_T
sed -i '$ d' $SOURCE_DIR"/"$BW_SUM_LOG_T
sed -i '1d' $SOURCE_DIR"/"$BW_SUM_LOG_T
if [ "$ACTIVE_SUBFLOWS" -eq 7 ]
then
  awk -F , -v OFS=, '$1=$2+$3+$4' <$SOURCE_DIR"/"$BW_SUM_LOG_T > $SOURCE_DIR"/"$BW_SUM_LOG
elif [ "$ACTIVE_SUBFLOWS" -eq 3 ]
then
  awk -F , -v OFS=, '$1=$2+$3' <$SOURCE_DIR"/"$BW_SUM_LOG_T > $SOURCE_DIR"/"$BW_SUM_LOG
else
  awk -F , -v OFS=, '$1=$2' <$SOURCE_DIR"/"$BW_SUM_LOG_T > $SOURCE_DIR"/"$BW_SUM_LOG
fi


#let the directory empty
eval rm $TARGET_DIR"/*"

if [ "$ACTIVE_SUBFLOWS" -gt 3 ]
then
  cp $SOURCE_DIR"/"$SUB3_SND_LOG $TARGET_DIR"/"$SUB3_SND_LOG
  cp $SOURCE_DIR"/"$SUB3_RCV_LOG $TARGET_DIR"/"$SUB3_RCV_LOG
  cp $SOURCE_DIR"/"$SUB3_BW_LOG $TARGET_DIR"/"$SUB3_BW_LOG
  cp $SOURCE_DIR"/"$SUB3_CTRLER_LOG $TARGET_DIR"/"$SUB3_CTRLER_LOG
fi

if [ "$ACTIVE_SUBFLOWS" -gt 1 ]
then
  cp $SOURCE_DIR"/"$SUB2_SND_LOG $TARGET_DIR"/"$SUB2_SND_LOG
  cp $SOURCE_DIR"/"$SUB2_RCV_LOG $TARGET_DIR"/"$SUB2_RCV_LOG
  cp $SOURCE_DIR"/"$SUB2_BW_LOG $TARGET_DIR"/"$SUB2_BW_LOG
  cp $SOURCE_DIR"/"$SUB2_CTRLER_LOG $TARGET_DIR"/"$SUB2_CTRLER_LOG
fi


cp $SOURCE_DIR"/"$SUB1_SND_LOG $TARGET_DIR"/"$SUB1_SND_LOG
cp $SOURCE_DIR"/"$SUB1_RCV_LOG $TARGET_DIR"/"$SUB1_RCV_LOG
cp $SOURCE_DIR"/"$SUB_RCV_SUM_LOG $TARGET_DIR"/"$SUB_RCV_SUM_LOG
cp $SOURCE_DIR"/"$SUB_SND_SUM_LOG $TARGET_DIR"/"$SUB_SND_SUM_LOG
cp $SOURCE_DIR"/"$SERVER_LOG $TARGET_DIR"/"$SERVER_LOG
cp $SOURCE_DIR"/"$CLIENT_LOG $TARGET_DIR"/"$CLIENT_LOG
cp $SOURCE_DIR"/"$SUB1_BW_LOG $TARGET_DIR"/"$SUB1_BW_LOG
cp $SOURCE_DIR"/"$SUB1_CTRLER_LOG $TARGET_DIR"/"$SUB1_CTRLER_LOG
cp $SOURCE_DIR"/"$BW_SUM_LOG $TARGET_DIR"/"$BW_SUM_LOG

#delete any tab in the file
if [ "$ACTIVE_SUBFLOWS" -gt 3 ]
then
  sed -i 's/\t//g' $TARGET_DIR"/"$SUB3_SND_LOG
  sed -i 's/\t//g' $TARGET_DIR"/"$SUB3_RCV_LOG
fi

if [ "$ACTIVE_SUBFLOWS" -gt 1 ]
then
  sed -i 's/\t//g' $TARGET_DIR"/"$SUB2_SND_LOG
  sed -i 's/\t//g' $TARGET_DIR"/"$SUB2_RCV_LOG
fi

sed -i 's/\t//g' $TARGET_DIR"/"$SUB1_SND_LOG
sed -i 's/\t//g' $TARGET_DIR"/"$SUB1_RCV_LOG
sed -i 's/\t//g' $TARGET_DIR"/"$SUB_RCV_SUM_LOG
sed -i 's/\t//g' $TARGET_DIR"/"$SUB_SND_SUM_LOG
sed -i 's/\t//g' $TARGET_DIR"/"$BW_SUM_LOG
#delete the comma at the end of each lines
if [ "$ACTIVE_SUBFLOWS" -gt 3 ]
then
  sed -i 's/,$//' $TARGET_DIR"/"$SUB3_SND_LOG
  sed -i 's/,$//' $TARGET_DIR"/"$SUB3_RCV_LOG
fi

if [ "$ACTIVE_SUBFLOWS" -gt 1 ]
then
  sed -i 's/,$//' $TARGET_DIR"/"$SUB2_SND_LOG
  sed -i 's/,$//' $TARGET_DIR"/"$SUB2_RCV_LOG
fi

sed -i 's/,$//' $TARGET_DIR"/"$SUB1_SND_LOG
sed -i 's/,$//' $TARGET_DIR"/"$SUB1_RCV_LOG
sed -i 's/,$//' $TARGET_DIR"/"$SUB_RCV_SUM_LOG
sed -i 's/,$//' $TARGET_DIR"/"$SUB_SND_SUM_LOG
sed -i 's/,$//' $TARGET_DIR"/"$BW_SUM_LOG
#delete the last line from the file
if [ "$ACTIVE_SUBFLOWS" -gt 3 ]
then
  sed -i '$ d' $TARGET_DIR"/"$SUB3_SND_LOG
  sed -i '$ d' $TARGET_DIR"/"$SUB3_RCV_LOG
fi

if [ "$ACTIVE_SUBFLOWS" -gt 1 ]
then
  sed -i '$ d' $TARGET_DIR"/"$SUB2_SND_LOG
  sed -i '$ d' $TARGET_DIR"/"$SUB2_RCV_LOG
fi

sed -i '$ d' $TARGET_DIR"/"$SUB1_SND_LOG
sed -i '$ d' $TARGET_DIR"/"$SUB1_RCV_LOG
sed -i '$ d' $TARGET_DIR"/"$SUB_RCV_SUM_LOG
sed -i '$ d' $TARGET_DIR"/"$SUB_SND_SUM_LOG
sed -i '$ d' $TARGET_DIR"/"$BW_SUM_LOG
#delete the first line from the file
if [ "$ACTIVE_SUBFLOWS" -gt 3 ]
then
  sed -i '1d' $TARGET_DIR"/"$SUB3_SND_LOG
  sed -i '1d' $TARGET_DIR"/"$SUB3_RCV_LOG
fi

if [ "$ACTIVE_SUBFLOWS" -gt 1 ]
then
  sed -i '1d' $TARGET_DIR"/"$SUB2_SND_LOG
  sed -i '1d' $TARGET_DIR"/"$SUB2_RCV_LOG
fi
sed -i '1d' $TARGET_DIR"/"$SUB1_SND_LOG
sed -i '1d' $TARGET_DIR"/"$SUB1_RCV_LOG
sed -i '1d' $TARGET_DIR"/"$SUB_RCV_SUM_LOG
sed -i '1d' $TARGET_DIR"/"$SUB_SND_SUM_LOG
sed -i '$ d' $TARGET_DIR"/"$BW_SUM_LOG

cd ../../Research-Scripts/gnuplot

if [ "$ACTIVE_SUBFLOWS" -eq 7 ]
then
  gnuplot -e "subflow_id='1'" -e "interface_id='0'" -e "foreman='"$FOREMAN"'" mprtp-subflow-report.plot
  gnuplot -e "subflow_id='2'" -e "interface_id='2'" -e "foreman='"$FOREMAN"'"  mprtp-subflow-report.plot
  gnuplot -e "subflow_id='3'" -e "interface_id='4'" -e "foreman='"$FOREMAN"'"  mprtp-subflow-report.plot
  gnuplot -e "subflow_num='3'" mprtp-sum-report.plot
elif [ "$ACTIVE_SUBFLOWS" -eq 3 ]
then
  gnuplot -e "subflow_id='1'" -e "interface_id='0'" -e "foreman='"$FOREMAN"'" mprtp-subflow-report.plot
  gnuplot -e "subflow_id='2'" -e "interface_id='2'" -e "foreman='"$FOREMAN"'" mprtp-subflow-report.plot
  gnuplot -e "subflow_num='2'" mprtp-sum-report.plot
else
  gnuplot -e "subflow_id='1'" -e "interface_id='0'" -e "foreman='"$FOREMAN"'" mprtp-subflow-report.plot
  gnuplot -e "subflow_num='1'" mprtp-sum-report.plot
fi
#gnuplot -e "subflow_id='1'" -e "interface_id='0'" mprtp-subflow-report.plot

cd ../../gst-mprtp/tests


