time=system("date +%Y_%m_%d_%H_%M_%S")

#---------------------------- Variables -----------------------------------

if (!exists("statfile")) statfile='statlogs.csv'
if (!exists("path_delay")) path_delay=0
if (!exists("output_file")) output_file='statlogs.pdf'
if (!exists("algorithm")) algorithm='scream'

duration=120

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


#Plot_2
set yrange [0:0.5]
set ytics 0.25
set xrange [0:duration]
set xtics 10 offset 0,-0.5
set format x "%3.0f"
set xtics font ", 26"

#set title "Network Queue (s)"
#unset title

set ylabel "Queue Delay [s]" offset -7,0 
set xlabel "Time [s]" offset 0,-1.1 font ", 22"

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot statfile using ($0*0.1):(($4 - path_delay)/1000000) with point pointtype 7 ps 0.3 lc rgb "blue" title "Queue Delay for Path 1", \
     statfile using ($0*0.1):(($8 - path_delay)/1000000) with point pointtype 7 ps 0.3 lc rgb "0x008c48" title "Queue Delay for Path 2"
  
set yrange [0:1.0]
set ytics 0.25
set xrange [0:duration]
set xtics 10 offset 0,-0.5
set xtics font ", 26"
set title "Lost Rates" font ",22" 
set ylabel "Fractional Lost" offset -7,0 font ", 22"
set format x "%3.0f"
set xlabel "Time [s]" offset 0,-1.1 font ", 22"

plot statfile using ($0*0.1):($9) with point pointtype 7 ps 0.3 lc rgb "blue" title "LR"
