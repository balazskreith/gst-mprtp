name=system("echo mprtp-subflow-") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("plot_title")) plot_title='Subflow Path ratios'
if (!exists("path_rates")) path_rates='logs/path_rates.csv'
if (!exists("subflow_num")) subflow_num=1
if (!exists("output_file")) output_file='reports/summary-path-rates.pdf'
if (!exists("duration")) duration=6000

#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 10,6
set output output_file

set origin 0,0
set size ratio 0.5
set datafile separator "," 

set key inside horizontal top left 
set tmargin 5
set bmargin 5
set lmargin 10
set rmargin 7
set yrange [0:1.1]
set ytics 0.1
set xrange [0:duration]
set xtics 500
set ylabel "Ratio"
set xlabel "time (100ms)"

set style fill solid
set style data boxes

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

if (subflow_num == 1) {
  plot path_rates using 0:1 with filledcurve x1 title "Path 1"
}

if (subflow_num == 2) {
  plot path_rates using 0:1 with filledcurve x1 title "Path 1", \
       path_rates using 0:2 with filledcurve x2 title "Path 2"
}

if (subflow_num == 3) {
  plot path_rates using 0:1 with filledcurve x1 title "Path 1", \
       path_rates using 0:2 with filledcurve x2 title "Path 2", \
       path_rates using 0:2 with filledcurve x3 title "Path 3"
}
