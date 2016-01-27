#!/bin/bash
#VETH0_CMDS="./run_veth0_cc.sh"
VETH0_CMDS="./$1"
VETH2_CMDS="./$2"
SERVER="./server-cctest"
CLIENT="./client"
OUTPUT1="test3.txt"
OUTPUT2="test3_awk.txt"
PLUSDAT="plusdat.txt"

sudo ip netns exec ns0 $VETH0_CMDS > veth0_bw.txt &
sudo ip netns exec ns0 $VETH2_CMDS > veth2_bw.txt &
sudo ip netns exec ns0 $SERVER "--profile=$3" &
sudo ip netns exec ns1 $CLIENT &

cleanup()
# example cleanup function
{
  pkill client
  pkill server-cctest
  ps -ef | grep $1 | grep -v grep | awk '{print }' | xargs kill
  ps -ef | grep $2 | grep -v grep | awk '{print }' | xargs kill
}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Ouch! Exiting ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

rm $PLUSDAT
touch $PLUSDAT
echo "0,0,0,0,|" >> $PLUSDAT
sleep 1
for j in `seq 1 10`;
do
  for i in `seq 1 60`;
  do
    echo "0,0,0,0,|" >> $PLUSDAT
    sleep 1
  done
  echo $j"th minute."
  ./run_test2.sh
done
echo "0,0,0,0,|" >> $PLUSDAT
sleep 1

cleanup

paste sefctrler.log refctrler.log veth0_bw.txt veth2_bw.txt $PLUSDAT > $OUTPUT1
cp $OUTPUT1 saved.txt 

#delete different file separators
sed -i 's/,|/,/g' $OUTPUT1
#delete any tab in the file
sed -i 's/\t//g' $OUTPUT1
#delete the comma at the end of each lines
sed -i 's/,$//' $OUTPUT1
#delete the last line from the file
sed -i '$ d' $OUTPUT1
#delete the first line from the file
sed -i '1d' $OUTPUT1

#aggregated bw limit
awk -F , -v OFS=, '$39=$37+$38' <$OUTPUT1 > $OUTPUT2
#aggregated goodput
awk -F , -v OFS=, '$40=$26+$34' < $OUTPUT2 > $OUTPUT1
#calculating ratios
awk -F , -v OFS=, '$41=$26/($26+$35)' <$OUTPUT1 > $OUTPUT2
#cp $OUTPUT1 $OUTPUT2
awk -F , -v OFS=, '$43=$35/($26+$35)' <$OUTPUT2 > $OUTPUT1
#cp $OUTPUT2 $OUTPUT1
awk -F , -v OFS=, '$2/=125' < $OUTPUT1 > $OUTPUT2
awk -F , -v OFS=, '$10/=125' < $OUTPUT2 > $OUTPUT1

#dividing delays
awk -F , -v OFS=, '$3/=1000000' < $OUTPUT1 > $OUTPUT2
awk -F , -v OFS=, '$4/=1000000' < $OUTPUT2 > $OUTPUT1
awk -F , -v OFS=, '$11/=1000000' < $OUTPUT1 > $OUTPUT2
awk -F , -v OFS=, '$12/=1000000' < $OUTPUT2 > $OUTPUT1

cp $OUTPUT1 ../../Research-Scripts/gnuplot/$OUTPUT1
cd ../../Research-Scripts/gnuplot
gnuplot mprtp-test3.plot 
cd ../../gst-mprtp/tests

DATE="Plots_"$(date +%y_%m_%d)
if [ ! -d "/home/balazs/Dropbox/MPRTP_Project/$DATE" ]; then
  mkdir "/home/balazs/Dropbox/MPRTP_Project/$DATE"
fi

TIME="test3_"$(date +%H_%M_%S)
cp ../../Research-Scripts/gnuplot/mprtp-test3.pdf /home/balazs/Dropbox/MPRTP_Project/$DATE/$TIME".pdf"
cp $OUTPUT1 /home/balazs/Dropbox/MPRTP_Project/$DATE/$TIME".txt"

