name=system("echo mprtp-subflow-") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("plot_title")) plot_title='Subflow Receiver Delays Report'
if (!exists("latency_file_1")) latency_file_1='logs/sub_1_rcv.csv'
if (!exists("output_file")) output_file='reports/summary_delays.pdf'
if (!exists("duration")) duration=6000

#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 10,6
set output output_file

set origin 0,0
set size ratio 0.5
set datafile separator "," 

set key inside horizontal top right 
set tmargin 0
set bmargin 5
set lmargin 7
set rmargin 7
set yrange [0:300000]
set ytics 100000
set xrange [0:duration]
set xtics 500

# set title plot_title font ",18"

# Line width of the axes
set border linewidth 0.1
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

if (!exists("latency_file_2")) {
  plot latency_file_1 using 0:3 with lines ls 1 title "Path 1 latency"
}else{
 if (!exists("latency_file_3")) {
    plot latency_file_1 using 0:3 with lines ls 1 title "Path 1 latency", \
         latency_file_2 using 0:3 with lines ls 2 title "Path 2 latency"
  }else{
      plot latency_file_1 using 0:3 with lines ls 1 title "Path 1 latency", \
           latency_file_2 using 0:3 with lines ls 2 title "Path 2 latency", \
           latency_file_3 using 0:3 with lines ls 3 title "Path 3 latency"
  }
} 

