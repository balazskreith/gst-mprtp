time=system("date +%Y_%m_%d_%H_%M_%S")


#---------------------------- Variables -----------------------------------

if (!exists("statfile")) statfile='statlogs.csv'
if (!exists("path_delay")) path_delay=0
if (!exists("output_file")) output_file='statlogs.pdf'
if (!exists("algorithm")) algorithm='scream'

duration=100

font_size=18
#-------------------------------------------------------------------------

set terminal pdf enhanced rounded size 18,6
set output output_file
set datafile separator "," 

set multiplot layout 2, 1 font ",18"
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
set key font ",22" 
set ytics font ",26" 

if (algorithm eq "scream"){
  #unset key
}

set yrange [0:3000]
set ytics 1000
set xrange [0:duration]
set xtics 10 offset 0,-1
set format x " "
unset xlabel

set ylabel "Throughput [kbps]" offset -7,0 font ", 22"

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

#statlogs using ($0*0.1):($5/125) with boxes lc rgb "0x008c48" title "FEC Rate", 
#unset key

if(algorithm eq "fractal") {

plot statfile using ($0*0.1):($2/125) with point pointtype 7 ps 0.3 lc rgb "blue" title "Sending Rate", \
     statfile using ($0*0.1):(($2+$3)/125) with point pointtype 7 ps 0.3 lc rgb "0x008c48" title "Sending Rate + FEC Rate", \
     statfile using ($0*0.1):1 with lines lc rgb "0xDC143C" title "Path Capacity"

}

if(algorithm eq "scream") {

plot statfile using ($0*0.1):($2/125) with point pointtype 7 ps 0.3 lc rgb "blue" title "Sending Rate", \
     statfile using ($0*0.1):1 with lines lc rgb "0xDC143C" title "Path Capacity"

}
     
#statlogs using ($0*0.1):($8/1000) with point pointtype 3 ps 0.3 lc rgb "0xFF6347" title "Target bitrate", \

#Plot_2
set yrange [0:0.5]
set ytics 0.25
set xrange [0:duration]
set xtics 10 offset 0,-0.5
set format x "%3.0f"
set xtics font ", 26"

#set title "Network Queue (s)"
#unset title

set ylabel "Queue Delay [s]" offset -7,0 font ", 22"
set xlabel "Time [s]" offset 0,-1.1 font ", 22"

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
  
plot statfile using ($0*0.1):(($4 - path_delay)/1000000) with point pointtype 7 ps 0.3 lc rgb "blue" title "Queue Delay"
  
  


