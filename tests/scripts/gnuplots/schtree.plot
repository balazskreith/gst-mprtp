name=system("echo treeplot") 
time=system("date +%Y_%m_%d_%H_%M_%S")

if (!exists("rnd_file")) rnd_file='logs/sub_snd_sum.csv'
if (!exists("tree_file")) tree_file='logs/sub_snd_sum.csv'
if (!exists("output_file")) output_file='reports/summary-snd-rates.pdf'


font_size=18
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 18,6
set output output_file
set datafile separator "," 

set multiplot layout 1, 2 font ",18"
set tmargin 4
set bmargin 5
set lmargin 20
set rmargin 10

#Plot_1
# magenta: #0xee2e2f
# green:   #0x008c48
# blue:    #0x185aa9
# orange:  #0xf47d23
# purple:  #0x662c91
# claret:  #0xa21d21
# lpurple: #0xb43894

#set title "Throughput (kbps)"
#unset title
set key font ",38" 
set ytics font ",48" 
set xtics font ",48" 
#unset key


unset xlabel

#set xtics nomirror
set style fill pattern 5 border
set xtics 250 offset 0,-1
#set format x " "
#set xlabel "Time (s)"
set yrange [0:1]
#set ylabel "Ratio"
set ytics 0.25


plot  rnd_file using 0:1 notitle with filledcurve x1, \
	  rnd_file using 0:1:($1+$3) notitle with filledcurve axis x1y1, \
	  rnd_file using 0:2 with lines lc rgb "0xDC143C" notitle
	  
unset ytics
plot  tree_file using 0:1 notitle with filledcurve x1, \
	  tree_file using 0:1:($1+$3) notitle with filledcurve axis x1y1, \
	  tree_file using 0:2 with lines lc rgb "0xDC143C" notitle


