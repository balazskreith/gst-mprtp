#!/bin/bash

function usage {
    echo "usage: $programname [-o|--output filename]"
    echo "	-s2 --subflow2	generates report for subflow 2"
    echo "	-s3 --subflow3	generates report for subflow 3"
    echo "	-o --output	determines output file"
    echo "	--author	determines the author of the report"
    echo "	--title 	determines the title of the report"
    echo "	--logdir 	determines the directory of logfiles"
    echo "	-tc --testcase	determines the test case the report generator report"
    exit 1
}


if [  $# -le 1 ] 
then 
  usage
  exit 1
fi 

echo "missed parameter" > reports/missing.txt

OUTPUT="reports/report.pdf"
SUBFLOW1=1
SUBFLOW2=0
SUBFLOW3=0
TESTCASE=0
REPORTTITLE="reports/missing.txt"
REPORTAUTHOR="reports/missing.txt"
LOGDIR="logs"

while [[ $# > 1 ]]
do
key="$1"

case $key in
    -s2|--subflow2)
    SUBFLOW2=1
    shift # past argument
    ;;
    -s3|--subflow3)
    SUBFLOW3=1
    ;;
    -o|--output)
    OUTPUT="$2"
    shift # past argument
    ;;
    -tc|--testcase)
    TESTCASE="$2"
    shift # past argument
    ;;
    --author)
    REPORTAUTHOR="$2"
    shift # past argument
    ;;
    --logdir)
    LOGDIR="$2"
    shift # past argument
    ;;
    --title)
    REPORTTITLE="$2"
    shift # past argument
    ;;
    --default)
    DEFAULT=YES
    ;;
    *)
            # unknown option
    ;;
esac
shift # past argument or value
done

#1 tick is 125ms
AUTOCORRDURATION=2400
#1 tick is 100ms
SUBRCVDELAYSDURATION=3000
SUBRCVRATESDURATION=3000
SUBSNDRATESDURATION=3000

#1 tick is one arrive
RTCPINTERVALDURATION=600

SUMMARYDELAYSDURATION=3000
SUMMARYSNDRATESDURATION=3000
SUMPATHRATESDURATION=3000
PLAYOUTSDURATION=300000
SUBFLOW1="1"

case $TESTCASE in
    0) 

    ;;
    1) 
    AUTOCORRDURATION=800
    SUBRCVDELAYSDURATION=1000
    SUBRCVRATESDURATION=1000
    RTCPINTERVALDURATION=100
    SUBSNDRATESDURATION=1000
    SUMMARYDELAYSDURATION=1000
    SUMMARYSNDRATESDURATION=1000
    SUMPATHRATESDURATION=1000
    PLAYOUTSDURATION=100000
    SUBFLOW1="1"
    SUBFLOW2=0
    SUBFLOW3=0
    ;;
    --default)

    ;;
    *)
    echo "No Bandwidth profile was given the default is used"
    ;;
esac

SUMPLAYOUT="reports/summary-playouts.pdf"
SUMDELAYS="reports/summary-path-delays.pdf"
SUMPATHRATES="reports/summary-path-rates.pdf"
SUMSNDRATES="reports/summary-snd-rates.pdf"
SUB1AUTOCORRS="reports/sub_1_mtau_corrs.pdf"
SUB2AUTOCORRS="reports/sub_2_mtau_corrs.pdf"
SUB3AUTOCORRS="reports/sub_3_mtau_corrs.pdf"
SUB1RCVDELAYS="reports/sub_1_delays.pdf"
SUB2RCVDELAYS="reports/sub_2_delays.pdf"
SUB3RCVDELAYS="reports/sub_3_delays.pdf"
SUB1RCVRATES="reports/sub_1_rcv_rates.pdf"
SUB2RCVRATES="reports/sub_2_rcv_rates.pdf"
SUB3RCVRATES="reports/sub_3_rcv_rates.pdf"
SUB1RTCPINTVALS="reports/rtcp_rr_1.pdf"
SUB2RTCPINTVALS="reports/rtcp_rr_2.pdf"
SUB3RTCPINTVALS="reports/rtcp_rr_2.pdf"
SUB1SNDRATES="reports/sub_1_snd_rates.pdf"
SUB2SNDRATES="reports/sub_2_snd_rates.pdf"
SUB3SNDRATES="reports/sub_3_snd_rates.pdf"


if [ $SUBFLOW1 = "1" ] && [ $SUBFLOW2 = "1" ] && [ $SUBFLOW3 = "1" ]; then
  echo "Generate reports for subflow[1|2|3]"

  gnuplot -e "duration='$SUMMARYDELAYSDURATION'" \
          -e "latency_file_1='$LOGDIR/sub_1_rcv.csv'" \
          -e "latency_file_2='$LOGDIR/sub_2_rcv.csv'" \
          -e "latency_file_3='$LOGDIR/sub_3_rcv.csv'" \
          -e "output_file='$SUMDELAYS'" \
          scripts/summary-delays.plot
  gnuplot -e "duration='$AUTOCORRDURATION'" \
          -e "autocorr_file='$LOGDIR/netqanalyser_1.csv'" \
          -e "output_file='$SUB1AUTOCORRS'" \
          scripts/subflow-autocorrs.plot
  gnuplot -e "duration='$AUTOCORRDURATION'" \
          -e "autocorr_file='$LOGDIR/netqanalyser_2.csv'" \
          -e "output_file='$SUB2AUTOCORRS'" \
          scripts/subflow-autocorrs.plot
  gnuplot -e "duration='$AUTOCORRDURATION'" \
          -e "autocorr_file='$LOGDIR/netqanalyser_3.csv'" \
          -e "output_file='$SUB3AUTOCORRS'" \
          scripts/subflow-autocorrs.plot

  gnuplot -e "duration='$SUBRCVDELAYSDURATION'" \
          -e "delays_file='$LOGDIR/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot
  gnuplot -e "duration='$SUBRCVDELAYSDURATION'" \
          -e "delays_file='$LOGDIR/sub_2_rcv.csv'" \
          -e "output_file='$SUB2RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot
  gnuplot -e "duration='$SUBRCVDELAYSDURATION'" \
          -e "delays_file='$LOGDIR/sub_3_rcv.csv'" \
          -e "output_file='$SUB3RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot

  gnuplot -e "duration='$SUBRCVRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVRATES'" \
          scripts/subflow-rcv-rates.plot
  gnuplot -e "duration='$SUBRCVRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_2_rcv.csv'" \
          -e "output_file='$SUB2RCVRATES'" \
          scripts/subflow-rcv-rates.plot
  gnuplot -e "duration='$SUBRCVRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_3_rcv.csv'" \
          -e "output_file='$SUB3RCVRATES'" \
          scripts/subflow-rcv-rates.plot

  gnuplot -e "duration='$RTCPINTERVALDURATION'" \
          -e "rtcp_file='$LOGDIR/rtcp_rr_1.csv'" \
          -e "output_file='$SUB1RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot
  gnuplot -e "duration='$RTCPINTERVALDURATION'" \
          -e "rtcp_file='$LOGDIR/rtcp_rr_2.csv'" \
          -e "output_file='$SUB2RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot
  gnuplot -e "duration='$RTCPINTERVALDURATION'" \
          -e "rtcp_file='$LOGDIR/rtcp_rr_3.csv'" \
          -e "output_file='$SUB3RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot

  gnuplot -e "duration='$SUBSNDRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_1_snd.csv'" \
          -e "output_file='$SUB1SNDRATES'" \
          scripts/subflow-snd-rates.plot
  gnuplot -e "duration='$SUBSNDRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_2_snd.csv'" \
          -e "output_file='$SUB2SNDRATES'" \
          scripts/subflow-snd-rates.plot
  gnuplot -e "duration='$SUBSNDRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_3_snd.csv'" \
          -e "output_file='$SUB3SNDRATES'" \
          scripts/subflow-snd-rates.plot

  gnuplot -e "duration='$SUMPATHRATESDURATION'" \
          -e "path_rates='$LOGDIR/path_rates.csv'" \
          -e "output_file='$SUMPATHRATES'" \
          -e "subflow_num='1'" \
          scripts/summary-path-rates.plot

  gnuplot -e "duration='$SUMMARYSNDRATESDURATION'" \
          -e "rate_file='$LOGDIR/sub_snd_sum.csv'" \
          -e "bw_file='$LOGDIR/veth_aggr.csv'" \
          -e "output_file='$SUMSNDRATES'" \
          scripts/summary-snd-rates.plot

  gnuplot -e "duration='$PLAYOUTSDURATION'" \
        -e "xtick_value='100000'" \
        -e "output='$SUMPLAYOUT'" \
        scripts/summary-playouts.plot



elif [ $SUBFLOW1 = "1" ] && [ $SUBFLOW2 = "1" ]; then
  echo "Generate reports for subflow[1|2]"
  gnuplot -e "duration='$SUMMARYDELAYSDURATION'" \
          -e "latency_file_1='$LOGDIR/sub_1_rcv.csv'" \
          -e "latency_file_2='$LOGDIR/sub_2_rcv.csv'" \
          -e "output_file='$SUMDELAYS'" \
          scripts/summary-delays.plot
  gnuplot -e "duration='$AUTOCORRDURATION'" \
          -e "autocorr_file='$LOGDIR/netqanalyser_1.csv'" \
          -e "output_file='$SUB1AUTOCORRS'"
          scripts/subflow-autocorrs.plot
  gnuplot -e "duration='$AUTOCORRDURATION'" \
          -e "autocorr_file='$LOGDIR/netqanalyser_2.csv'" \
          -e "output_file='$SUB2AUTOCORRS'" \
          scripts/subflow-autocorrs.plot

  gnuplot -e "duration='$SUBRCVDELAYSDURATION'" \
          -e "delays_file='$LOGDIR/sub_1_rcv.csv'" \
          -e "output='$SUB1RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot
  gnuplot -e "duration='$SUBRCVDELAYSDURATION'" \
          -e "delays_file='$LOGDIR/sub_2_rcv.csv'" \
          -e "output_file='$SUB2RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot

  gnuplot -e "duration='$SUBRCVRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVRATES'" \
          scripts/subflow-rcv-rates.plot
  gnuplot -e "duration='$SUBRCVRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_2_rcv.csv'" \
          -e "output='$SUB2RCVRATES'" \
          scripts/subflow-rcv-rates.plot

   gnuplot -e "duration='$RTCPINTERVALDURATION'" \
          -e "rtcp_file='$LOGDIR/rtcp_rr_1.csv'" \
          -e "output_file='$SUB1RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot
  gnuplot -e "duration='$RTCPINTERVALDURATION'" \
          -e "rtcp_file='$LOGDIR/rtcp_rr_2.csv'" \
          -e "output_file='$SUB2RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot

  gnuplot -e "duration='$SUBSNDRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_1_snd.csv'" \
          -e "output_file='$SUB1SNDRATES'" \
          scripts/subflow-snd-rates.plot
  gnuplot -e "duration='$SUBSNDRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_2_snd.csv'" \
          -e "output_file='$SUB2SNDRATES'" \
          scripts/subflow-snd-rates.plot

  gnuplot -e "duration='$SUMPATHRATESDURATION'" \
          -e "path_rates='$LOGDIR/path_rates.csv'" \
          -e "output_file='$SUMPATHRATES'" \
          -e "subflow_num='1'" \
          scripts/summary-path-rates.plot

  gnuplot -e "duration='$SUMMARYSNDRATESDURATION'" \
          -e "rate_file='$LOGDIR/sub_snd_sum.csv'" \
          -e "bw_file='$LOGDIR/veth_aggr.csv'" \
          -e "output_file='$SUMSNDRATES'" \
          scripts/summary-snd-rates.plot


  gnuplot -e "duration='$PLAYOUTSDURATION'" \
          -e "xtick_value='100000'" \
          -e "output='$SUMPLAYOUT'" \
          scripts/summary-playouts.plot

elif [ $SUBFLOW1 = "1" ]; then

  echo "Generate reports for subflow[1]"
  gnuplot -e "duration='$SUMMARYDELAYSDURATION'" \
          -e "latency_file_1='$LOGDIR/sub_1_rcv.csv'" \
          -e "output_file='$SUMDELAYS'" \
          scripts/summary-delays.plot

  gnuplot -e "duration='$AUTOCORRDURATION'" \
          -e "autocorr_file='$LOGDIR/netqanalyser_1.csv'" \
          -e "output_file='$SUB1AUTOCORRS'" \
          scripts/subflow-autocorrs.plot

  gnuplot -e "duration='$SUBRCVDELAYSDURATION'" \
          -e "delays_file='$LOGDIR/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot

  gnuplot -e "duration='$SUBRCVRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVRATES'" \
          scripts/subflow-rcv-rates.plot

  gnuplot -e "duration='$RTCPINTERVALDURATION'" \
          -e "rtcp_file='$LOGDIR/rtcp_rr_1.csv'" \
          -e "output_file='$SUB1RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot

  gnuplot -e "duration='$SUBSNDRATESDURATION'" \
          -e "rates_file='$LOGDIR/sub_1_snd.csv'" \
          -e "output_file='$SUB1SNDRATES'" \
          scripts/subflow-snd-rates.plot

  gnuplot -e "duration='$SUMPATHRATESDURATION'" \
          -e "path_rates='$LOGDIR/path_rates.csv'" \
          -e "output_file='$SUMPATHRATES'" \
          -e "subflow_num='1'" \
          scripts/summary-path-rates.plot

  gnuplot -e "duration='$SUMMARYSNDRATESDURATION'" \
          -e "rate_file='$LOGDIR/sub_snd_sum.csv'" \
          -e "bw_file='$LOGDIR/veth0.csv'" \
          -e "output_file='$SUMSNDRATES'" \
          scripts/summary-snd-rates.plot

  gnuplot -e "duration='$PLAYOUTSDURATION'" \
          -e "xtick_value='100000'" \
          -e "output='$SUMPLAYOUT'" \
          scripts/summary-playouts.plot

fi

#Do the report.pdf

#copy tex template

cp reports_template/main.tex $LOGDIR/report.tex
#make a tmp dir for tex strings
TMPDIR="$LOGDIR/tmp"
mkdir $LOGDIR/tmp

#generate the texts for the test
./scripts/generate_tex_texts.sh -o $LOGDIR/tmp -tc $TESTCASE


sed -e '/TESTCASEANDTOPOLOGY/ {' -e 'r '$TMPDIR'/TESTCASEANDTOPOLOGY.txt' -e 'd' -e '}' -i $LOGDIR/report.tex
TESTCASEANDTOPOLOGY=$(cat "$TMPDIR/TESTCASEANDTOPOLOGY.txt")
#sed -i -e 's/TITLEREPLACESTRING/'$REPORTTITLE'/g' $LOGDIR/report.tex
#sed -i -e 's/AUTHORREPLACESTRING/'$REPORTAUTHOR'/g' $LOGDIR/report.tex
sed -e '/TITLEREPLACESTRING/ {' -e 'r '$REPORTTITLE -e 'd' -e '}' -i $LOGDIR/report.tex
sed -e '/AUTHORREPLACESTRING/ {' -e 'r '$REPORTAUTHOR -e 'd' -e '}' -i $LOGDIR/report.tex

sed -i -e 's/SUMMARYREPORTREPLACESTRING/Summary./g' $LOGDIR/report.tex

#echo "\includepdf[hb]{"$SUMSNDRATES"}" > $TMPDIR"/tmp.txt"
echo "\includegraphics[scale=0.6]{"$SUMSNDRATES"}" > $TMPDIR"/tmp.txt"
sed -e '/SUMMARYSENDINGRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

echo "\includegraphics[scale=0.5]{"$SUMPLAYOUT"}" > $TMPDIR"/tmp.txt"
sed -e '/SUMMARYPLAYOUTDELAYREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

echo "\includegraphics[scale=0.6]{"$SUMPATHRATES"}" > $TMPDIR"/tmp.txt"
sed -e '/SUMMARYPATHRATIOSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

sed -i -e 's/SUBFLOW1SUMMARYREPLACESTRING/Summary./g' $LOGDIR/report.tex
echo "\includegraphics[scale=0.6]{"$SUB1SNDRATES"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1SENDERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

echo "\includegraphics[scale=0.6]{"$SUB1RCVRATES"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1RECEIVERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

echo "\includegraphics[scale=0.6]{"$SUB1RTCPINTVALS"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1RTCPINTERVALSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

echo "\includegraphics[scale=0.6]{"$SUB1RCVDELAYS"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1LATENCIESREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

echo "\includegraphics[scale=0.4]{"$SUB1AUTOCORRS"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1MULTIPLETAUCORRELATIONREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

#if now subflow 2 or 3 elliminate that parts
#sed -n '/SUBFLOW2STARTMARKER/{p; :a; N; /SUBFLOW2ENDMARKER/!ba; s/.*\n//}; p' $LOGDIR/report.tex


if [ $SUBFLOW2 = "1" ] && [ $SUBFLOW3 = "1" ]; then

	sed -i -e 's/SUBFLOW2SUMMARYREPLACESTRING/Summary./g' $LOGDIR/report.tex
	echo "\includegraphics[scale=0.6]{"$SUB1SNDRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2SENDERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1RCVRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2RECEIVERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1RTCPINTVALS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2RTCPINTERVALSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1RCVDELAYS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2LATENCIESREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6\textheight]{"$SUB1AUTOCORRS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2MULTIPLETAUCORRELATIONREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	sed -i -e 's/SUBFLOW3SUMMARYREPLACESTRING/Summary./g' $LOGDIR/report.tex
	echo "\includegraphics[scale=0.6]{"$SUB1SNDRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3SENDERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1RCVRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3RECEIVERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1RTCPINTVALS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3RTCPINTERVALSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1RCVDELAYS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3LATENCIESREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6\textheight]{"$SUB1AUTOCORRS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3MULTIPLETAUCORRELATIONREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

elif [ $SUBFLOW2 = "1" ]; then

	sed -i -e 's/SUBFLOW2SUMMARYREPLACESTRING/Summary./g' $LOGDIR/report.tex
	echo "\includegraphics[scale=0.6]{"$SUB1SNDRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2SENDERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1RCVRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2RECEIVERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1RTCPINTVALS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2RTCPINTERVALSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1RCVDELAYS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2LATENCIESREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex

	echo "\includegraphics[scale=0.6]{"$SUB1AUTOCORRS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2MULTIPLETAUCORRELATIONREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i $LOGDIR/report.tex


  sed -n -i -e '/SUBFLOW3STARTMARKER/{' -e 'p' -e ':a' -e 'N' -e '/SUBFLOW3ENDMARKER/!ba' -e 's/.*\n//' -e '}' -e 'p' $LOGDIR/report.tex
  sed -i -e 's/SUBFLOW3STARTMARKER/./g' $LOGDIR/report.tex
  sed -i -e 's/SUBFLOW3ENDMARKER/./g' $LOGDIR/report.tex

else

  sed -n -i -e '/SUBFLOW2STARTMARKER/{' -e 'p' -e ':a' -e 'N' -e '/SUBFLOW2ENDMARKER/!ba' -e 's/.*\n//' -e '}' -e 'p' $LOGDIR/report.tex
  sed -i -e 's/SUBFLOW2STARTMARKER/./g' $LOGDIR/report.tex
  sed -i -e 's/SUBFLOW2ENDMARKER/./g' $LOGDIR/report.tex

  sed -n -i -e '/SUBFLOW3STARTMARKER/{' -e 'p' -e ':a' -e 'N' -e '/SUBFLOW3ENDMARKER/!ba' -e 's/.*\n//' -e '}' -e 'p' $LOGDIR/report.tex
  sed -i -e 's/SUBFLOW3STARTMARKER/./g' $LOGDIR/report.tex
  sed -i -e 's/SUBFLOW3ENDMARKER/./g' $LOGDIR/report.tex

fi



pdflatex $LOGDIR/report.tex 
mv report.pdf reports/report.pdf
#delete the tmp dir
rm -R $LOGDIR/tmp





