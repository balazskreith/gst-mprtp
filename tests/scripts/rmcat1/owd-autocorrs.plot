name=system("echo mprtp-subflow-") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("plot_title"))  plot_title='Subflow Multiple-tau autocorrelations'
if (!exists("autocorr_file")) autocorr_file='logs/netqanalyser_1.csv'
if (!exists("output_file")) output_file='reports/sub_1_mtau_corrs.pdf'
if (!exists("duration"))  duration=1000
if (!exists("xtick_value"))  xtick_value=600

#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 16,10
set output output_file

set origin 0,0
set size ratio 0.1
set datafile separator "," 

set key inside horizontal top right 
unset title

set tmargin 2
set bmargin 0
set lmargin 15
set rmargin 3
set xrange [0:duration]
set yrange [0:1000]
set ytics 250
set ylabel "time (ms)"
#set xtics xtick_value
unset xlabel
unset xrange

#set multiplot layout 6,1 title plot_title
set multiplot layout 6,1 font ",18"

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
set style line 5 linecolor rgb '#f47d23' linetype 4 linewidth 1	

plot autocorr_file using 0:($1/1000) with lines ls 1 title "Reported OWD"

set key inside horizontal top right 
set tics scale 0
set ylabel "Autocorr"
set yrange [-0.1:0.3]
set ytics  0.05
set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
set xtics

plot autocorr_file using 0:2 with lines ls 2 title "g(100ms)"

plot autocorr_file using 0:3 with lines ls 3 title "g(200ms)"

plot autocorr_file using 0:4 with lines ls 4 title "g(400ms)"

set xlabel "running length (100ms)"

plot autocorr_file using 0:5 with lines ls 5 title "g(800ms)"

unset multiplot