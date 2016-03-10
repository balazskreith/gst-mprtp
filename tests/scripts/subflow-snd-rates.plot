name=system("echo mprtp-subflow-") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("plot_title")) plot_title='Subflow Sending report'
if (!exists("rates_file")) rates_file='logs/sub_1_snd.csv'
if (!exists("bw_file")) bw_file='logs/veth0.csv'
if (!exists("output_file")) output_file='reports/sub_1_snd.pdf'
if (!exists("csv_length")) csv_length=6000

#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 10,6
set output output_file

set origin 0,0
set size ratio 0.5
set datafile separator "," 

set key inside horizontal top left 
set tmargin 5
set bmargin 5
set lmargin 7
set rmargin 7
set yrange [0:3000]
set ytics 500
set xrange [0:csv_length]
set xlabel "Rates (Kbits/s)"
set ylabel "time (ms)"
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
plot rates_file using 0:1 with lines ls 1 title "Sender", \
     rates_file using 0:2 with lines ls 2 title "Monitor", \
     rates_file using 0:3 with lines ls 3 title "Target", \
     bw_file    using 0:1 with lines ls 4 title "Capacity"

