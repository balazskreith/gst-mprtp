name=system("echo mprtp-subflow-") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("plot_title")) plot_title='Subflow Receiver Rate Report'
if (!exists("throughput_file")) throughput_file='logs/sub_snd_sum.csv'
if (!exists("output_file")) output_file='reports/summary-snd-rates.pdf'
if (!exists("duration")) duration=6000
if (!exists("range")) range=3000
if (!exists("flownum")) flownum=1


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
set size ratio 0.5
set datafile separator "," 


if (!exists("legends")) {
  unset key
}else{
  set key inside vertical top right box opaque
  set border back
}
   
set tmargin 5
set bmargin 5

if (!exists("labels")){
  set lmargin 12  
}else{
  set lmargin 23
}

set rmargin 7
set yrange [0:range]
set ytics 1000
set xrange [0:duration]
set xtics 50 offset 0,-1
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

set style line 1 linecolor rgb '#008c48' linetype 1 linewidth 1
set style line 2 linecolor rgb '#b43894' linetype 2 linewidth 1
set style line 3 linecolor rgb '#185aa9' linetype 3 linewidth 1
set style line 4 linecolor rgb '#a21d21' linetype 4 linewidth 1	
set style line 5 linecolor rgb '#662c91' linetype 5 linewidth 1	
set style line 6 linecolor rgb '#ffffff' linetype 6 linewidth 1	
set style line 7 linecolor rgb '#000000' linetype 7 linewidth 1	

if (flownum == 1){
  plot throughput_file using ($0*0.1):(($1+$2+$19)/125) with filledcurve x1 ls 4 title "TCP Rate", \
       throughput_file using ($0*0.1):(($1+$2)/125) with filledcurve x1 ls 6 notitle, \
       throughput_file using ($0*0.1):(($1+$2)/125) with lines ls 7 title "Sending Rate"
}

if (flownum == 2){
    plot throughput_file using ($0*0.1):(($1+$2)/125) with lines ls 1 title "Sending Rate", \
         throughput_file using ($0*0.1):(($7+$8)/125) with lines ls 2 title "Sending Rate 2", \
         throughput_file using ($0*0.1):22 with lines ls 4 title "Path Capacity"
}
