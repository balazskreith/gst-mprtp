time=system("date +%Y_%m_%d_%H_%M_%S")


#---------------------------- Variables -----------------------------------

if (!exists("statlogs")) statlogs='statlogs.csv'
if (!exists("path_delay")) path_delay=0
if (!exists("output_file")) output_file='statlogs.pdf'

duration=100

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

set yrange [0:3000]
set ytics 1000
set xrange [0:duration]
set xtics 10 offset 0,-1
set format x " "
unset xlabel

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

#unset key
plot statlogs using ($0*0.1 + 5):($4/125) with point pointtype 7 ps 0.2 lc rgb "blue" title "Sending Rate", \
     statlogs using ($0*0.1):($5/125) with boxes lc rgb "0x008c48" title "FEC Rate", \
     statlogs using ($0*0.1):($13/125) with point pointtype 7 ps 0.2 lc rgb "0xFF6347" title "TCP Rate", \
     statlogs using ($0*0.1):2 with lines lc rgb "0xDC143C" title "Path Capacity"
     
#statlogs using ($0*0.1):($8/1000) with point pointtype 3 ps 0.05 lc rgb "0xFF6347" title "Target bitrate", \

#Plot_2
set yrange [0:1]
set ytics 0.5
set xrange [0:duration]
set xtics 10 offset 0,-1

set title "Network Queue (s)"
unset xlabel

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot statlogs using ($0*0.1):(($6 - path_delay)/1000000) with point pointtype 7 ps 0.2 lc rgb "blue" title "Queue Delay"
  
  
#Plot_3
set yrange [0:0.5]
set ytics 0.25
set xrange [0:duration]
set xtics 10 offset 0,-1

set title "Playout delays"
unset xlabel

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot statlogs using ($0*0.1):($7/1000000) with point pointtype 7 ps 0.2 lc rgb "blue" title "Playout delay (80th)"  
  

unset multiplot
#
#
#
