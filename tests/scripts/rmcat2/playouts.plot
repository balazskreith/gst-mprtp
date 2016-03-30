name=system("echo mprtp-subflow-") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("plot_title"))     plot_title='Playouts summary'
if (!exists("playouts_file"))  playouts_file='logs/playouts.csv'
if (!exists("skew_file"))      skew_file='logs/skews.csv'
if (!exists("output_file"))    output_file='reports/summary-playouts.pdf'
if (!exists("duration"))       duration=600000
if (!exists("xtick_value"))    xtick_value=100000

#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 12,6
set output output_file

set origin 0,0
set size ratio 0.2
set datafile separator "," 

set key inside horizontal top right 
unset title

set tmargin 2
set bmargin 0
set lmargin 15
set rmargin 5
set xrange [0:duration]
set yrange [-10:200]
set ytics 50
set ylabel "Remaining time (ms)"

#set multiplot layout 2,1 title plot_title
set multiplot layout 3,1 font ",18"

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

set xrange [0:duration]
set xtics xtick_value
unset xlabel

plot playouts_file using 0:($1/1000000) with lines ls 1 title "Playout delay", \
     skew_file u 0:($1/1000000) w lines ls 2 title "Path 1 skew" 

set key inside horizontal top right 
set tics scale 0
set ylabel "Size (KBytes)"
set yrange [0:100]
set ytics  20
set xrange [0:duration]

plot playouts_file using 0:($2/1000) with lines ls 2 title "Playout buffer"

unset multiplot