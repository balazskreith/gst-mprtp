name=system("echo mprtp-subflow-") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("throughput_file")) throughput_file='logs/sub_snd_sum.csv'
if (!exists("owd_file")) throughput_file='logs/owd.csv'
if (!exists("owd_file2")) throughput_file='logs/owd2.csv'
if (!exists("output_file")) output_file='reports/summary-snd-rates.pdf'
if (!exists("labels")) labels='0'
if (!exists("legends")) legends='0'

duration=120
range=3000

font_size=18
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 10,6
set output output_file
set key font ",32"
set xtics font ",32"
set ytics font ",32"
set ylabel font ",32"
set xlabel font ",32"
 
set origin 0,0
set size ratio 0.25
set datafile separator "," 

set multiplot layout 2,1 font ",18"

if (legends < 1) {
  unset key
}else{
  set key inside vertical top right
}
   
#set tmargin 5
#set bmargin 5

if (labels < 1){
  set lmargin 12  
}else{
  set lmargin 23
}

set rmargin 7
set yrange [0:range]
set ytics 1000
set xrange [0:duration]
set xtics 20 offset 0,-1
if (0 < labels){
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

set style line 1 linecolor rgb '#008c48' linetype 1 linewidth 1
set style line 2 linecolor rgb '#b43894' linetype 2 linewidth 1
set style line 3 linecolor rgb '#185aa9' linetype 3 linewidth 1
set style line 4 linecolor rgb '#a21d21' linetype 4 linewidth 1	
set style line 5 linecolor rgb '#662c91' linetype 5 linewidth 1	

unset xlabel
unset xtics  
plot throughput_file using ($0*0.1):(($1+$2)/125) with lines ls 1 title "SR (Peer 1)", \
     throughput_file using ($0*0.1):(($6+$7)/125) with lines ls 2 title "SR (Peer 2)", \
     throughput_file using ($0*0.1):22 with lines ls 4 title "Path Capacity"
       
       
set yrange [0:500]
set ytics 100
set xrange [0:duration]
set xtics 20 offset 0,-1
set xtics font ", 32"
set xlabel font ", 32"

if (0 < labels){
  set ylabel "Delay (ms)" offset -8
}
set xlabel "Time (s)" offset 0,-2
set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot owd_file using ($0*0.1):(($2)/1000) with lines ls 1 title "OWD (Peer1)", \
     owd_file2 using ($0*0.1):(($2)/1000) with lines ls 2 title "OWD (Peer2)", \
  
unset multiplot
