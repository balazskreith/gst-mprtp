name=system("echo mprtp-subflow-") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("throughput_file")) throughput_file='logs/sub_snd_sum.csv'
if (!exists("owd_file")) throughput_file='logs/owd.csv'
if (!exists("output_file")) output_file='reports/summary-snd-rates.pdf'
if (!exists("path_delay")) path_delay=50000

duration=100
range=2900

font_size=18
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 10,6
set output output_file
set key font ",20"
set xtics font ",20"
set ytics font ",20"
set ylabel font ",20"
set xlabel font ",20"
 
set origin 0,0
set size ratio 0.23
set datafile separator "," 

set multiplot layout 2,1 font ",18"

if (!exists("legends")) {
  unset key
}else{
  set key inside vertical top right
}
   
   
#set lmargin at screen 0.0
#set rmargin at screen 0.85
#set bmargin at screen 0.05
#set tmargin at screen 0.85

#set tmargin 3
#set bmargin 5

if (!exists("labels")){
  set lmargin 2  
}else{
  set lmargin 23
}
set rmargin 3
set yrange [0:range]
set ytics 1000
set xrange [0:duration]
set xtics 10 offset 0,-1
if (exists("labels")){
  set ylabel "Throughput (KBits)" offset -8
}
set xlabel "Time (s)" offset 0,-2
set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

# Line width of the axes
# Line styles
#colors:
# magenta: #ee2e2f
# green:   #008c48
# blue:    #185aa9
# orange:  #f47d23
# purple:  #662c91
# claret:  #a21d21
# lpurple: #b43894

set style line 1 linecolor rgb '#008c48' linetype 1 linewidth 2
set style line 2 linecolor rgb '#b43894' linetype 2 linewidth 1
set style line 3 linecolor rgb '#185aa9' linetype 3 linewidth 1
set style line 4 linecolor rgb '#a21d21' linetype 4 linewidth 1	
set style line 5 linecolor rgb '#662c91' linetype 5 linewidth 1	

unset xlabel
set format x ""
#unset xtics  

set title "Throughput (kbps)" font ", 20"

plot throughput_file using ($0*0.1):($1/125) with lines ls 1 title "Sending Rate", \
     throughput_file using ($0*0.1):($2/125) with boxes ls 3 title "FEC Rate", \
     throughput_file using ($0*0.1):22 with lines ls 4 title "Path Capacity"
       
       
set yrange [0:1]
set ytics 0.5
set xrange [0:duration]
set xtics 10 offset 0,-1
set xtics font ", 20"
set xlabel font ", 20"

unset format x 

unset title
set title "Network Delay (s)" font ", 20"
 
if (exists("labels")){
  set ylabel "Delay (ms)" offset -8
}
set xlabel "Time (s)" offset 0,-1
set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot owd_file using ($0*0.1):(($1 - path_delay)/1000000) with point pointtype 7 ps 0.2 lc rgb "blue" title "Queue Delay"
  
unset multiplot
