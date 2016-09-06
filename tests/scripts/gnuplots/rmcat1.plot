time=system("date +%Y_%m_%d_%H_%M_%S")


#---------------------------- Variables -----------------------------------

if (!exists("throughput_file")) throughput_file='logs/sub_snd_sum.csv'
if (!exists("owd_file")) throughput_file='logs/owd.csv'
if (!exists("output_file")) output_file='reports/summary-snd-rates.pdf'
if (!exists("path_delay")) path_delay=50000
if (!exists("fecstat_file")) fecstat_file='logs/fecstat.csv'

duration=100
range=2900

font_size=18
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 10,6
set output output_file
set datafile separator "," 

set multiplot layout 3, 1 font ",14"
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
set format x " "
unset xlabel

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

unset key
plot throughput_file using ($0*0.1):($1/125) with point pointtype 7 ps 0.2 lc rgb "blue" title "Sending Rate", \
     throughput_file using ($0*0.1):($2/125) with boxes lc rgb "0x008c48" title "FEC Rate", \
     throughput_file using ($0*0.1):22 with lines lc rgb "0xDC143C" title "Path Capacity"


#Plot_2
set yrange [0:1]
set ytics 0.5
set xrange [0:duration]
set xtics 10 offset 0,-1

set title "Network Delay (s)"
unset xlabel

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot owd_file using ($0*0.1):(($1 - path_delay)/1000000) with point pointtype 7 ps 0.2 lc rgb "blue" title "Queue Delay"
  
  

#Plot_3

set title "FFRE"
set yrange [0:1.1]
set ytics 0.25
set xlabel "Time (s)" offset 0,-1
set format x "%.0f"

plot fecstat_file using ($0*0.1):(0 < $2+$3 ? $3/($2+$3) : 0) with filledcurve x1 lc rgb "blue" title "Packet Recovery Efficiency"


#
unset multiplot
#
#
#
