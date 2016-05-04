name=system("echo mprtp-subflow-") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("plot_title")) plot_title='Subflow Losts Report'
if (!exists("stat_file")) stat_file='logs/fecdec_stat.csv'
if (!exists("output_file")) output_file='reports/summary-losts.pdf'
if (!exists("duration")) duration=6000

#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 12,6
set output output_file

set origin 0,0
set size ratio 0.5 
set datafile separator "," 

set key inside horizontal top right 
unset title

set tmargin 2
set bmargin 5
set lmargin 10
set rmargin 5
set xrange [0:duration]
set yrange [0:1.1]
set ytics 0.2
set xtics 500
set ylabel "Ratio"
set xlabel "time (100ms)"
set xrange [0:duration]
unset xlabel


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

plot stat_file using 0:2 with lines ls 1 title "fraction lost", \
     stat_file u 0:4 w lines ls 2 t "discarded rate"

set xlabel "time (100ms)"

unset multiplot