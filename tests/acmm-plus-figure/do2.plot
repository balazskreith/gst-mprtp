time=system("date +%Y_%m_%d_%H_%M_%S")


#---------------------------- Variables -----------------------------------

font_size=28
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 18,6
set output 'do2.pdf'
set datafile separator "," 

set multiplot layout 2, 1 font ",18"
set tmargin 4
set bmargin 6
set lmargin 20
set rmargin 10

# magenta: #0xee2e2f
# green:   #0x008c48
# blue:    #0x185aa9
# orange:  #0xf47d23
# purple:  #0x662c91
# claret:  #0xa21d21
# lpurple: #0xb43894


#Plot_1
set yrange [0:500]
set xrange [0:9000]
set xtics 1000 offset 0,-1
set ytics 250
set format x "%3.0f"
set key font ",28" 
set xtics font ", 28"
set ytics font ", 28"

#set title "Network Queue (s)" font "arial, 30" 

set ylabel "delay [ms]" font "arial, 30" offset -9,0
set xlabel "packets number" font "arial, 30" offset 0,-2
unset xlabel

plot 'without_jitter/out.csv' using 1 with point pointtype 7 ps 0.3 lc rgb "blue" title "Packet delay", \
	 'without_jitter/out.csv' using 0:($2+50) with point pointtype 7 ps 0.1 lc rgb "red" title "network delay + packet delay deviation"
  
  
#Plot_2
set yrange [0:500]
set xrange [0:9000]
set xtics 1000 offset 0,-1
set format x "%3.0f"
set xtics font ", 28"

#set title "Network Queue (s)" font "arial, 30" 

set ylabel "delay [ms]" font "arial, 30" offset -9,0
set xlabel "packets number" font "arial, 30" offset 0,-2

plot 'with_jitter/out.csv' using 1 with point pointtype 7 ps 0.3 lc rgb "blue" title "Packet delay", \
	 'with_jitter/out.csv' using 0:($2+50) with point pointtype 7 ps 0.1 lc rgb "red" title "network delay + packet delay deviation"
  
  

