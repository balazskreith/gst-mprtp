time=system("date +%Y_%m_%d_%H_%M_%S")


#---------------------------- Variables -----------------------------------

if (!exists("statlogs")) statlogs='statlogs.csv'
if (!exists("statlogs2")) statlogs2='statlogs2.csv'
if (!exists("path_delay")) path_delay=0
if (!exists("output_file")) output_file='statlogs.pdf'

duration=120

font_size=18
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 18,10
set output output_file
set datafile separator "," 

set multiplot layout 3, 1 font ",18"
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
set key font ",38" horizontal
set ytics font ",48" 

#if (algorithm eq "scream"){
  #unset key
#}

set yrange [0:3000]
set ytics 1000
set xrange [0:duration]
set xtics 10 offset 0,-1
set format x " "
unset xlabel


set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

#unset key

if(algorithm eq "fractal") {

plot statlogs using ($0*0.1):(($4+$5)/125) with point pointtype 7 ps 0.2 lc rgb "blue" title "Sending Rate + FEC Rate", \
     statlogs  using ($0*0.1):2 with lines lc rgb "0xDC143C" title "Path Capacity"

plot statlogs2 using ($0*0.1):(($4+$5)/125) with point pointtype 7 ps 0.2 lc rgb "0x008c48" title "Sending Rate + FEC Rate", \
	 statlogs2 using ($0*0.1):15 with lines lc rgb "0xFF6347" title "Path Capacity "

}
     
if(algorithm eq "scream") {

plot statlogs  using ($0*0.1):($4/125) with point pointtype 7 ps 0.2 lc rgb "blue" title "Sending Rate", \
     statlogs  using ($0*0.1):3 with lines lc rgb "0xDC143C" title "Path Capacity"

plot statlogs2 using ($0*0.1):($4/125) with point pointtype 7 ps 0.2 lc rgb "0x008c48" title "Sending Rate", \
	 statlogs2 using ($0*0.1):2 with lines lc rgb "0xFF6347" title "Path Capacity "


}

#Plot_2
set yrange [0:1]
set ytics 0.25
set xrange [0:duration]
set xtics 10 offset 0,-1
set format x "%3.0f"
set xtics font ", 36"

#set title "Network Queue (s)"
unset xlabel

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot statlogs using ($0*0.1):(($6 - path_delay)/1000000) with point pointtype 7 ps 0.4 lc rgb "blue"      title "Queue Delay", \
	 statlogs2 using ($0*0.1):(($6 - path_delay)/1000000) with point pointtype 7 ps 0.4 lc rgb "0x008c48" title "Queue Delay2"                  
  
 
