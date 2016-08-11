time=system("date +%Y_%m_%d_%H_%M_%S")


#---------------------------- Variables -----------------------------------

if (!exists("throughput_file")) throughput_file='logs/sub_snd_sum.csv'
if (!exists("owd_file")) owd_file='logs/owd.csv'
if (!exists("owd_file2")) owd_file2='logs/owd2.csv'
if (!exists("owd_file3")) owd_file3='logs/owd3.csv'
if (!exists("owd_file4")) owd_file2='logs/owd4.csv'
if (!exists("owd_file5")) owd_file2='logs/owd5.csv'
if (!exists("output_file")) output_file='reports/summary-snd-rates.pdf'

duration=120
range=3500

font_size=18
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 10,6
set output output_file
set datafile separator "," 

set multiplot layout 2, 1 font ",14"
set tmargin 4

#Plot_1
# magenta: #0xee2e2f
# green:   #0x008c48
# blue:    #0x185aa9
# orange:  #0xf47d23
# purple:  #0x662c91
# claret:  #0xa21d21
# lpurple: #0xb43894

set title "Throughput (kbps)"

set yrange [0:range]
set ytics 1000
set xrange [0:duration]
set xtics 10 offset 0,-1
set format x ""
unset xlabel

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

unset key
plot throughput_file using ($0*0.1):(($1+$2)/125) with point pointtype 7 ps 0.2 lc rgb "blue" title "Sending Rate", \
	 throughput_file using ($0*0.1):(($7+$8)/125) with point pointtype 7 ps 0.2 lc rgb "0x008c48" title "Sending Rate", \
	 throughput_file using ($0*0.1):(($13+$14)/125) with point pointtype 7 ps 0.2 lc rgb "red" title "Sending Rate", \
	 throughput_file using ($0*0.1):(($19+$20)/125) with point pointtype 7 ps 0.2 lc rgb "0xf47d23" title "Sending Rate", \
	 throughput_file using ($0*0.1):(($25+$26)/125) with point pointtype 7 ps 0.2 lc rgb "0xb43894" title "Sending Rate"


#Plot_2
set yrange [0:1]
set ytics 0.5
set xrange [0:duration]
set xtics 10 offset 0,-1

set title "One Way Delay Measurements (s)"
unset xlabel

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot owd_file using ($0*0.1):(($1)/1000000) with point pointtype 7 ps 0.2 lc rgb "blue" title "Queue Delay", \
	 owd_file2 using ($0*0.1):(($1)/1000000) with point pointtype 7 ps 0.2 lc rgb "0x008c48" title "Queue Delay", \
	 owd_file3 using ($0*0.1):(($1)/1000000) with point pointtype 7 ps 0.2 lc rgb "red" title "Queue Delay", \
	 owd_file4 using ($0*0.1):(($1)/1000000) with point pointtype 7 ps 0.2 lc rgb "0xf47d23" title "Queue Delay", \
	 owd_file5 using ($0*0.1):(($1)/1000000) with point pointtype 7 ps 0.2 lc rgb "0xb43894" title "Queue Delay"
  
  

#
unset multiplot
#
#
#
