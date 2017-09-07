time=system("date +%Y_%m_%d_%H_%M_%S")


#---------------------------- Variables -----------------------------------

font_size=28
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 18,6
set output 'do.pdf'
set datafile separator "," 

set multiplot layout 2, 1 font ",18"
set tmargin 4
set bmargin 6
set lmargin 20
set rmargin 10

#Plot_1
# magenta: #0xee2e2f
# green:   #0x008c48
# blue:    #0x185aa9
# orange:  #0xf47d23
# purple:  #0x662c91
# claret:  #0xa21d21
# lpurple: #0xb43894

#set title "Throughput (kbps)"
#unset title
set key font ",28" 
set ytics font ",38" 


set yrange [0:1700]
set ytics 500
set xrange [0:100]
set xtics 10 offset 0,-1
set xtics font ", 28"
set format x "%3.0f"
set xlabel

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set ylabel "throughput [kbit/s]" font "arial, 30" offset -9,0
#set xlabel "time [s]" font "arial, 30" offset 105,-2
set xlabel "time [s]" font "arial, 30" offset 0,-2

plot 'sr.csv' using ($0*0.1):($1/125) with point pointtype 7 ps 0.3 lc rgb "blue" title "Sending Rate", \
     'pathbw.csv' using ($0*0.1):1 with lines lc rgb "0xDC143C" title "Path Capacity"

     
#statlogs using ($0*0.1):($8/1000) with point pointtype 3 ps 0.3 lc rgb "0xFF6347" title "Target bitrate", \

#Plot_2
set yrange [0:500]
set xrange [0:9000]
set xtics 1000 offset 0,-1
set format x "%3.0f"
set xtics font ", 28"

set title "Network Queue (s)" font "arial, 30" 
unset title

set ylabel "delay [ms]" font "arial, 30" offset -9,0
#set xlabel "packets number" font "arial, 30" offset 100,-2
set xlabel "packets number" font "arial, 30" offset 0,-2

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot 'out.csv' using 1 with point pointtype 7 ps 0.3 lc rgb "blue" title "Packet delay", \
	 'out.csv' using 0:($2+50) with point pointtype 7 ps 0.05 lc rgb "red" title "network delay + packet delay deviation"
  
  

