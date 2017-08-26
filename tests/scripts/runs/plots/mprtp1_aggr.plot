time=system("date +%Y_%m_%d_%H_%M_%S")

#---------------------------- Variables -----------------------------------

if (!exists("statfile")) statfile='statlogs.csv'
if (!exists("path_delay")) path_delay=0
if (!exists("output_file")) output_file='statlogs.pdf'
if (!exists("algorithm")) algorithm='scream'

duration=100

font_size=18
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 18,6
set output output_file
set datafile separator "," 

set multiplot layout 2, 1 font ",18"
set tmargin 4
set bmargin 5
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
set key font ",22" 
set ytics font ",26" 

set yrange [0:4000]
set ytics 1000
set xrange [0:duration]
set xtics 10 offset 0,-1
set format x " "
unset xlabel

set ylabel "Throughput [kbps]" offset -7,0 font ", 22"

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

#unset key

plot statfile using ($0*0.1):(($2+$3+$6+$7)/125) with point pointtype 7 ps 0.3 lc rgb "0xf47d23" title "Total (SR + FEC)", \
     statfile using ($0*0.1):($1+$5) with lines lc rgb "red" lw 2 title "Aggr Capacity"


set yrange [0:1.2]
set ytics 0.25
set xrange [0:duration]
set xtics 10 offset 0,-0.5
set xtics font ", 26"
set format x " "
set key horizontal top right

set title "Load balancing" font ",22" 
set ylabel "Ratio" offset -7,0 font ", 22"

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot statfile using ($0*0.1):(1) with filledcurves x1 lc rgb "0x008c48" title "Subflow 2", \
     statfile using ($0*0.1):(($2+$3)/($2+$3+$6+$7)) with filledcurves x1 lc rgb "blue" title "Subflow 1"
