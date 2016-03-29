#!/bin/bash
programname=$0

function usage {
    echo "usage: $programname [-r|-rtpprofile num]"
    echo "	-r --rtprofile		determines the rtp testing profile"
    echo "				        equal to the ./server --profile=profile_num"
    echo "  -p --period             determines the period of the report generation"
    echo "  --savnam			the name of the saving"
    echo "  --savdir			the directory of the saving"
    exit 1
}
 
if [[ $# < 2 ]]
then
  usage
fi


RPROFILE=73
REPPERIOD=5
SAVDIR="0"
SAVNAM="0"

while [[ $# > 1 ]]
do
key="$1"
case $key in
    -r|--rtpprofile)
    RPROFILE="$2"
    shift # past argument
    ;;
    -p|--period)
    REPPERIOD="$2"
    shift # past argument
    ;;
    --savdir)
    SAVDIR="$2"
    shift # past argument
    ;;
    --savnam)
    SAVNAM="$2"
    shift # past argument
    ;;     
    --default)
    ;;
    *)
            # unknown option
    ;;
esac
shift # past argument or value
done


echo ".-------------------------------------------------------------."
echo "| Test starts with the following parameters                   |"
echo "| RTP profile:   "$RPROFILE
echo "| Report period: "$REPPERIOD
echo "'-------------------------------------------------------------'"


NSSND="ns_snd"
NSRCV="ns_rcv"
SERVER="./server"
CLIENT="./client"
LOGSDIR="logs"
LOGSDIR2="logs2"
REPORTSDIR="reports"
REPORTEXFILE="report.tex"
STATFILE="stats.csv"
REPORTPDF="report.pdf"

SCRIPTSDIR="scripts"
TESTDIR="$SCRIPTSDIR/rmcat2"

rm -R $LOGSDIR
rm -R $LOGSDIR2
mkdir $LOGSDIR
mkdir $LOGSDIR2

#Report author
REPORTAUTHORFILE=$LOGSDIR"/author.txt"
echo "Balázs Kreith" > $REPORTAUTHORFILE

  #setup duration
  DURATION=120
  
  #setup virtual ethernet interface controller script
  echo "./scripts/veth_ctrler.sh --veth 0 --output $LOGSDIR/veth0.csv --input $TESTDIR/veth0.csv --roothandler 1 --leafhandler 2" > scripts/test_bw_veth0_snd.sh
  chmod 777 scripts/test_bw_veth0_snd.sh

  #run a virtual ethernet interface controller script
  sudo ip netns exec $NSSND ./scripts/test_bw_veth0_snd.sh &

  echo "./server --profile=$RPROFILE --logsdir=$LOGSDIR/  --path1_tx_rtp_port=5000 --path1_tx_rtcp_port=5001 --path1_rx_rtp_port=5002 --path1_rx_rtcp_port=5003 --path2_tx_rtp_port=5004 --path2_tx_rtcp_port=5005 --path2_rx_rtp_port=5006 --path2_rx_rtcp_port=5007 --path3_tx_rtp_port=5008 --path3_tx_rtcp_port=5009 --path3_rx_rtp_port=5010 --path3_rx_rtcp_port=5011 --rtpbin_tx_rtcp_port=5013 --rtpbin_rx_rtcp_port=5015" > "$TESTDIR/server1.sh"
  echo "./server --profile=$RPROFILE --logsdir=$LOGSDIR2/ --path1_tx_rtp_port=5016 --path1_tx_rtcp_port=5017 --path1_rx_rtp_port=5018 --path1_rx_rtcp_port=5019 --path2_tx_rtp_port=5020 --path2_tx_rtcp_port=5021 --path2_rx_rtp_port=5022 --path2_rx_rtcp_port=5023 --path3_tx_rtp_port=5024 --path3_tx_rtcp_port=5025 --path3_rx_rtp_port=5026 --path3_rx_rtcp_port=5027 --rtpbin_tx_rtcp_port=5028 --rtpbin_rx_rtcp_port=5029" > "$TESTDIR/server2.sh"
  				 
  echo "./client --profile=$RPROFILE --logsdir=$LOGSDIR/  --path1_tx_rtp_port=5000 --path1_tx_rtcp_port=5001 --path1_rx_rtp_port=5002 --path1_rx_rtcp_port=5003 --path2_tx_rtp_port=5004 --path2_tx_rtcp_port=5005 --path2_rx_rtp_port=5006 --path2_rx_rtcp_port=5007 --path3_tx_rtp_port=5008 --path3_tx_rtcp_port=5009 --path3_rx_rtp_port=5010 --path3_rx_rtcp_port=5011 --rtpbin_tx_rtcp_port=5013 --rtpbin_rx_rtcp_port=5015" > "$TESTDIR/client1.sh"
  echo "./client --profile=$RPROFILE --logsdir=$LOGSDIR2/ --path1_tx_rtp_port=5016 --path1_tx_rtcp_port=5017 --path1_rx_rtp_port=5018 --path1_rx_rtcp_port=5019 --path2_tx_rtp_port=5020 --path2_tx_rtcp_port=5021 --path2_rx_rtp_port=5022 --path2_rx_rtcp_port=5023 --path3_tx_rtp_port=5024 --path3_tx_rtcp_port=5025 --path3_rx_rtp_port=5026 --path3_rx_rtcp_port=5027 --rtpbin_tx_rtcp_port=5028 --rtpbin_rx_rtcp_port=5029" > "$TESTDIR/client2.sh"
				 
  chmod 777 $TESTDIR/client2.sh
  chmod 777 $TESTDIR/client1.sh
  chmod 777 $TESTDIR/server2.sh
  chmod 777 $TESTDIR/server1.sh
  
  #start client and server
  sudo ip netns exec $NSRCV $TESTDIR/client2.sh 2> $LOGSDIR"/"client1.log &
  sudo ip netns exec $NSRCV $TESTDIR/client1.sh 2> $LOGSDIR2"/"client2.log &
  sleep 1
  sudo ip netns exec $NSSND $TESTDIR/server2.sh 2> $LOGSDIR2"/"server2.log &
  sudo ip netns exec $NSSND $TESTDIR/server1.sh 2> $LOGSDIR"/"server1.log &

  echo "
  while true; do 
    #./$TESTDIR/plots.sh --srcdir $LOGSDIR --dstdir $REPORTSDIR
    #./$TESTDIR/stats.sh --srcdir $LOGSDIR --dst $REPORTSDIR/$STATFILE
    #mv $LOGSDIR/ccparams_1.log $REPORTSDIR/ccparams_1.log
    #./$TESTDIR/report.sh --srcdir $REPORTSDIR --author $REPORTAUTHORFILE --dst $REPORTEXFILE
    #./$SCRIPTSDIR/pdflatex.sh $REPORTEXFILE

    #mv $REPORTPDF $REPORTSDIR/$REPORTPDF
    sleep $REPPERIOD
  done

  " > scripts/auto_rep_generator.sh

  chmod 777 scripts/auto_rep_generator.sh


cleanup()
# example cleanup function
{
  pkill client
  pkill server
  ps -ef | grep 'veth_ctrler.sh' | grep -v grep | awk '{print $2}' | xargs kill
  ps -ef | grep 'report_generato' | grep -v grep | awk '{print $2}' | xargs kill
  ps -ef | grep 'main.sh' | grep -v grep | awk '{print $2}' | xargs kill

}
 
control_c()
# run if user hits control-c
{
  echo -en "\n*** Exiting run_test.sh ***\n"
  cleanup
  exit $?
}

trap control_c SIGINT

./$SCRIPTSDIR/auto_rep_generator.sh  > report.log &

sleep $DURATION

if [ "$SAVDIR" != "0" ]
then
  echo "./$TESTDIR/save.sh --logsdir $LOGSDIR --repsdir $REPORTSDIR --savnam $SAVNAM --savdir $SAVDIR" > $SCRIPTSDIR/saving.sh
  chmod 777 $SCRIPTSDIR/saving.sh
  ./$SCRIPTSDIR/saving.sh
fi

cleanup


