#!/bin/bash

function usage {
    echo "usage: $programname [-o|--output filename]"
    echo "	-s2 --subflow2	generates report for subflow 2"
    echo "	-s3 --subflow3	generates report for subflow 3"
    echo "	-o --output	determines output file"
    exit 1
}


if [  $# -le 1 ] 
then 
  usage
  exit 1
fi 

OUTPUT="reports/report.pdf"
SUBFLOW2=0
SUBFLOW3=0

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
    --default)
    DEFAULT=YES
    ;;
    *)
            # unknown option
    ;;
esac
shift # past argument or value
done


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
#generate the plots
gnuplot -e "csv_length='600000'" \
        -e "xtick_value='100000'" \
        -e "output='$SUMPLAYOUT'" \
        scripts/summary-playouts.plot

if [ $SUBFLOW2 = "1" ] && [ $SUBFLOW3 = "1" ]; then
  echo "Generate reports for subflow[1|2|3]"
  gnuplot -e "csv_length='6000'" \
          -e "latency_file_1='logs/sub_1_rcv.csv'" \
          -e "latency_file_2='logs/sub_2_rcv.csv'" \
          -e "latency_file_3='logs/sub_3_rcv.csv'" \
          -e "output_file='$SUMDELAYS'" \
          scripts/summary-delays.plot
  gnuplot -e "csv_length='1000'" \
          -e "autocorr_file='logs/netqanalyser_1.csv'" \
          -e "output_file='$SUB1AUTOCORRS'" \
          scripts/subflow-autocorrs.plot
  gnuplot -e "csv_length='1000'" \
          -e "autocorr_file='logs/netqanalyser_2.csv'" \
          -e "output_file='$SUB2AUTOCORRS'" \
          scripts/subflow-autocorrs.plot
  gnuplot -e "csv_length='1000'" \
          -e "autocorr_file='logs/netqanalyser_3.csv'" \
          -e "output_file='$SUB3AUTOCORRS'" \
          scripts/subflow-autocorrs.plot

  gnuplot -e "csv_length='1000'" \
          -e "delays_file='logs/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot
  gnuplot -e "csv_length='1000'" \
          -e "delays_file='logs/sub_2_rcv.csv'" \
          -e "output_file='$SUB2RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot
  gnuplot -e "csv_length='1000'" \
          -e "delays_file='logs/sub_3_rcv.csv'" \
          -e "output_file='$SUB3RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot

  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVRATES'" \
          scripts/subflow-rcv-rates.plot
  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_2_rcv.csv'" \
          -e "output_file='$SUB2RCVRATES'" \
          scripts/subflow-rcv-rates.plot
  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_3_rcv.csv'" \
          -e "output_file='$SUB3RCVRATES'" \
          scripts/subflow-rcv-rates.plot

  gnuplot -e "csv_length='1000'" \
          -e "rtcp_file='logs/rtcp_rr_1.csv'" \
          -e "output_file='$SUB1RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot
  gnuplot -e "csv_length='1000'" \
          -e "rtcp_file='logs/rtcp_rr_2.csv'" \
          -e "output_file='$SUB2RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot
  gnuplot -e "csv_length='1000'" \
          -e "rtcp_file='logs/rtcp_rr_3.csv'" \
          -e "output_file='$SUB3RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot

  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_1_snd.csv'" \
          -e "output_file='$SUB1SNDRATES'" \
          scripts/subflow-snd-rates.plot
  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_2_snd.csv'" \
          -e "output_file='$SUB2SNDRATES'" \
          scripts/subflow-snd-rates.plot
  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_3_snd.csv'" \
          -e "output_file='$SUB3SNDRATES'" \
          scripts/subflow-snd-rates.plot

  gnuplot -e "csv_length='6000'" \
          -e "path_rates='logs/path_rates.csv'" \
          -e "output_file='$SUMPATHRATES'" \
          -e "subflow_num='1'" \
          scripts/summary-path-rates.plot

  gnuplot -e "csv_length='6000'" \
          -e "rate_file='logs/sub_snd_sum.csv'" \
          -e "bw_file='logs/veth_aggr.csv'" \
          -e "output_file='$SUMSNDRATES'" \
          scripts/summary-snd-rates.plot


elif [ $SUBFLOW2 = "1" ]; then
  echo "Generate reports for subflow[1|2]"
  gnuplot -e "csv_length='6000'" \
          -e "latency_file_1='logs/sub_1_rcv.csv'" \
          -e "latency_file_2='logs/sub_2_rcv.csv'" \
          -e "output_file='$SUMDELAYS'" \
          scripts/summary-delays.plot
  gnuplot -e "csv_length='1000'" \
          -e "autocorr_file='logs/netqanalyser_1.csv'" \
          -e "output_file='$SUB1AUTOCORRS'"
          scripts/subflow-autocorrs.plot
  gnuplot -e "csv_length='1000'" \
          -e "autocorr_file='logs/netqanalyser_2.csv'" \
          -e "output_file='$SUB2AUTOCORRS'" \
          scripts/subflow-autocorrs.plot

  gnuplot -e "csv_length='1000'" \
          -e "delays_file='logs/sub_1_rcv.csv'" \
          -e "output='$SUB1RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot
  gnuplot -e "csv_length='1000'" \
          -e "delays_file='logs/sub_2_rcv.csv'" \
          -e "output_file='$SUB2RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot

  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVRATES'" \
          scripts/subflow-rcv-rates.plot
  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_2_rcv.csv'" \
          -e "output='$SUB2RCVRATES'" \
          scripts/subflow-rcv-rates.plot

  gnuplot -e "csv_length='1000'" \
          -e "rtcp_file='logs/rtcp_rr_1.csv'" \
          -e "output_file='$SUB1RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot
  gnuplot -e "csv_length='1000'" \
          -e "rtcp_file='logs/rtcp_rr_2.csv'" \
          -e "output_file='$SUB2RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot

  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_1_snd.csv'" \
          -e "output_file='$SUB1SNDRATES'" \
          scripts/subflow-snd-rates.plot
  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_2_snd.csv'" \
          -e "output_file='$SUB2SNDRATES'" \
          scripts/subflow-snd-rates.plot

  gnuplot -e "csv_length='6000'" \
          -e "path_rates='logs/path_rates.csv'" \
          -e "output_file='$SUMPATHRATES'" \
          -e "subflow_num='1'" \
          scripts/summary-path-rates.plot

  gnuplot -e "csv_length='6000'" \
          -e "rate_file='logs/sub_snd_sum.csv'" \
          -e "bw_file='logs/veth_aggr.csv'" \
          -e "output_file='$SUMSNDRATES'" \
          scripts/summary-snd-rates.plot
else
  echo "Generate reports for subflow[1]"
  gnuplot -e "csv_length='6000'" \
          -e "latency_file_1='logs/sub_1_rcv.csv'" \
          -e "output_file='$SUMDELAYS'" \
          scripts/summary-delays.plot

  gnuplot -e "csv_length='1000'" \
          -e "autocorr_file='logs/netqanalyser_1.csv'" \
          -e "output_file='$SUB1AUTOCORRS'" \
          scripts/subflow-autocorrs.plot

  gnuplot -e "csv_length='1000'" \
          -e "delays_file='logs/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVDELAYS'" \
          scripts/subflow-rcv-delays.plot

  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_1_rcv.csv'" \
          -e "output_file='$SUB1RCVRATES'" \
          scripts/subflow-rcv-rates.plot

  gnuplot -e "csv_length='1000'" \
          -e "rtcp_file='logs/rtcp_rr_1.csv'" \
          -e "output_file='$SUB1RTCPINTVALS'" \
          scripts/subflow-rtcp-intervals.plot

  gnuplot -e "csv_length='6000'" \
          -e "rates_file='logs/sub_1_snd.csv'" \
          -e "output_file='$SUB1SNDRATES'" \
          scripts/subflow-snd-rates.plot

  gnuplot -e "csv_length='6000'" \
          -e "path_rates='logs/path_rates.csv'" \
          -e "output_file='$SUMPATHRATES'" \
          -e "subflow_num='1'" \
          scripts/summary-path-rates.plot

  gnuplot -e "csv_length='6000'" \
          -e "rate_file='logs/sub_snd_sum.csv'" \
          -e "bw_file='logs/veth0.csv'" \
          -e "output_file='$SUMSNDRATES'" \
          scripts/summary-snd-rates.plot

fi


#Do the report.pdf

#copy tex template

cp reports_template/main.tex logs/report.tex
#make a tmp dir for tex strings
TMPDIR="logs/tmp"
mkdir logs/tmp

#generate the texts for the test
./scripts/generate_tex_texts.sh -o logs/tmp -c 0


sed -e '/TESTCASEANDTOPOLOGY/ {' -e 'r '$TMPDIR'/TESTCASEANDTOPOLOGY.txt' -e 'd' -e '}' -i logs/report.tex
TESTCASEANDTOPOLOGY=$(cat "$TMPDIR/TESTCASEANDTOPOLOGY.txt")
sed -i -e 's/TITLEREPLACESTRING/Automatic MPRTP test report/g' logs/report.tex
sed -i -e 's/AUTHORREPLACESTRING/Balázs Kreith/g' logs/report.tex

sed -i -e 's/SUMMARYREPORTREPLACESTRING/Summary./g' logs/report.tex

#echo "\includepdf[hb]{"$SUMSNDRATES"}" > $TMPDIR"/tmp.txt"
echo "\includegraphics[width=1.3\textwidth]{"$SUMSNDRATES"}" > $TMPDIR"/tmp.txt"
sed -e '/SUMMARYSENDINGRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

echo "\includegraphics[width=1.3\textwidth]{"$SUMPLAYOUT"}" > $TMPDIR"/tmp.txt"
sed -e '/SUMMARYPLAYOUTDELAYREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

echo "\includegraphics[width=1.3\textwidth]{"$SUMPATHRATES"}" > $TMPDIR"/tmp.txt"
sed -e '/SUMMARYPATHRATIOSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

sed -i -e 's/SUBFLOW1SUMMARYREPLACESTRING/Summary./g' logs/report.tex
echo "\includegraphics[width=1.3\textwidth]{"$SUB1SNDRATES"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1SENDERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

echo "\includegraphics[width=1.2\textwidth]{"$SUB1RCVRATES"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1RECEIVERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

echo "\includegraphics[width=1.2\textwidth]{"$SUB1RTCPINTVALS"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1RTCPINTERVALSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

echo "\includegraphics[width=1.2\textwidth]{"$SUB1RCVDELAYS"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1LATENCIESREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

echo "\includegraphics[width=1.2\textwidth,height=0.6\textheight]{"$SUB1AUTOCORRS"}" > $TMPDIR"/tmp.txt"
sed -e '/SUBFLOW1MULTIPLETAUCORRELATIONREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

#if now subflow 2 or 3 elliminate that parts
#sed -n '/SUBFLOW2STARTMARKER/{p; :a; N; /SUBFLOW2ENDMARKER/!ba; s/.*\n//}; p' logs/report.tex


if [ $SUBFLOW2 = "1" ] && [ $SUBFLOW3 = "1" ]; then

	sed -i -e 's/SUBFLOW2SUMMARYREPLACESTRING/Summary./g' logs/report.tex
	echo "\includegraphics[width=1.3\textwidth]{"$SUB1SNDRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2SENDERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth]{"$SUB1RCVRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2RECEIVERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth]{"$SUB1RTCPINTVALS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2RTCPINTERVALSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth]{"$SUB1RCVDELAYS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2LATENCIESREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth,height=0.6\textheight]{"$SUB1AUTOCORRS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2MULTIPLETAUCORRELATIONREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	sed -i -e 's/SUBFLOW3SUMMARYREPLACESTRING/Summary./g' logs/report.tex
	echo "\includegraphics[width=1.3\textwidth]{"$SUB1SNDRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3SENDERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth]{"$SUB1RCVRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3RECEIVERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth]{"$SUB1RTCPINTVALS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3RTCPINTERVALSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth]{"$SUB1RCVDELAYS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3LATENCIESREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth,height=0.6\textheight]{"$SUB1AUTOCORRS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW3MULTIPLETAUCORRELATIONREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

elif [ $SUBFLOW2 = "1" ]; then

	sed -i -e 's/SUBFLOW2SUMMARYREPLACESTRING/Summary./g' logs/report.tex
	echo "\includegraphics[width=1.3\textwidth]{"$SUB1SNDRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2SENDERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth]{"$SUB1RCVRATES"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2RECEIVERRATEREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth]{"$SUB1RTCPINTVALS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2RTCPINTERVALSREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth]{"$SUB1RCVDELAYS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2LATENCIESREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex

	echo "\includegraphics[width=1.2\textwidth,height=0.6\textheight]{"$SUB1AUTOCORRS"}" > $TMPDIR"/tmp.txt"
	sed -e '/SUBFLOW2MULTIPLETAUCORRELATIONREPLACESTRING/ {' -e 'r '$TMPDIR'/tmp.txt' -e 'd' -e '}' -i logs/report.tex


  sed -n -i -e '/SUBFLOW3STARTMARKER/{' -e 'p' -e ':a' -e 'N' -e '/SUBFLOW3ENDMARKER/!ba' -e 's/.*\n//' -e '}' -e 'p' logs/report.tex
  sed -i -e 's/SUBFLOW3STARTMARKER/./g' logs/report.tex
  sed -i -e 's/SUBFLOW3ENDMARKER/./g' logs/report.tex

else

  sed -n -i -e '/SUBFLOW2STARTMARKER/{' -e 'p' -e ':a' -e 'N' -e '/SUBFLOW2ENDMARKER/!ba' -e 's/.*\n//' -e '}' -e 'p' logs/report.tex
  sed -i -e 's/SUBFLOW2STARTMARKER/./g' logs/report.tex
  sed -i -e 's/SUBFLOW2ENDMARKER/./g' logs/report.tex

  sed -n -i -e '/SUBFLOW3STARTMARKER/{' -e 'p' -e ':a' -e 'N' -e '/SUBFLOW3ENDMARKER/!ba' -e 's/.*\n//' -e '}' -e 'p' logs/report.tex
  sed -i -e 's/SUBFLOW3STARTMARKER/./g' logs/report.tex
  sed -i -e 's/SUBFLOW3ENDMARKER/./g' logs/report.tex

fi



pdflatex logs/report.tex 
mv report.pdf reports/report.pdf
#delete the tmp dir
rm -R logs/tmp





