import subprocess
from command import *
import os
import logging
import threading

from numpy import genfromtxt

import pandas as pd

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

import numpy as np

class SrQmdPlot:
    def __init__(self, pathbw_csv, path_delay, output_file, duration):
        self.__path_delay = path_delay
        self.__duration = duration
        self.__output_file = output_file
        self.__pathbw_csv = pathbw_csv
        self.__plots = []
        pass

    def add_plot(self, sr_csv, qmd_csv, sr_title = "Sending Rate", qmd_title = "Path Delay", plot_fec = False,
        fec_title = "Sending Rate + FEC", start_delay = "0"):
        self.__plots.append({
            "sr_csv": sr_csv,
            "sr_title": sr_title,
            "qmd_csv": qmd_csv,
            "qmd_title": qmd_title,
            "plot_fec": plot_fec,
            "fec_title": fec_title,
            "start_delay": start_delay
            })

    @property
    def plots(self):
        return self.__plots

    @property
    def path_delay(self):
        return self.__path_delay

    @property
    def pathbw_csv(self):
        return self.__pathbw_csv

    @property
    def output_file(self):
        return self.__output_file

    @property
    def duration(self):
        return self.__duration


class SrQmdGnuPlot(SrQmdPlot):
    def __init__(self, pathbw_csv, path_delay, output_file, duration):
        SrQmdPlot.__init__(self, pathbw_csv, path_delay, output_file, duration)

    def generate(self, plot_file = "temp/sr_qmd.plot"):
        colors = ["blue", "0x008c48", "#0xf47d23", "#0x662c91", "#0xa21d21", "#0xb43894"]
        commands = []
        commands.append('time=system("date +%Y_%m_%d_%H_%M_%S")')

        commands.append('path_delay=' + str(int(self.path_delay) * 1000 ) )
        commands.append("output_file='" + self.output_file + "'")

        commands.append('duration=' + str(self.duration))

        commands.append('font_size=18')

        commands.append('set terminal pdf enhanced rounded size 18,6')
        commands.append('set output output_file')
        commands.append('set datafile separator ","')

        commands.append('set multiplot layout 2, 1 font ",18"')
        commands.append('set tmargin 4')
        commands.append('set bmargin 5')
        commands.append('set lmargin 20')
        commands.append('set rmargin 10')

        commands.append('set key font ",22"')
        commands.append('set ytics font ",26"')


        commands.append('set yrange [0:3000]')
        commands.append('set ytics 1000')
        commands.append('set xrange [0:duration]')
        commands.append('set xtics 10 offset 0,-1')
        commands.append('set format x " "')
        commands.append('unset xlabel')

        commands.append('set ylabel "Throughput [kbps]" offset -7,0 font ", 22"')

        commands.append('set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"')
        commands.append('set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"')

        plot_written = False
        colors_index = 0
        for plot in self.plots:
            color = colors[colors_index]
            if (plot_written == False):
                commands.append('plot \'' + plot["sr_csv"] + '\' using ($0*0.1 + ' + plot["start_delay"] + '):($1/125) with point pointtype 7 ps 0.3 lc rgb "' + color + '" title "' + plot["sr_title"] + '",' + " \\")
                plot_written = True
            else:
                commands.append('\t\'' + plot["sr_csv"] + '\' using ($0*0.1 + ' + plot["start_delay"] + '):($1/125) with point pointtype 7 ps 0.3 lc rgb "' + color + '" title "' + plot["sr_title"] + '",' + " \\")

            if (plot["plot_fec"]):
                colors_index = colors_index + 1
                color = colors[colors_index]
                commands.append('\t\'' + plot["sr_csv"] + '\' using ($0*0.1 + ' + plot["start_delay"] + '):(($1+$2)/125) with point pointtype 7 ps 0.3 lc rgb "' + color + '" title "' + plot["fec_title"] + '",' + " \\")

            colors_index = colors_index + 1

        commands.append('\t\'' + self.pathbw_csv + '\' using ($0*0.1):1 with lines lc rgb "0xDC143C" title "Path Capacity"')

        commands.append('set yrange [0:0.5]')
        commands.append('set ytics 0.25')
        commands.append('set xrange [0:duration]')
        commands.append('set xtics 10 offset 0,-0.5')
        commands.append('set format x "%3.0f"')
        commands.append('set xtics font ", 26"')


        commands.append('set ylabel "Queue Delay [s]" offset -7,0 font ", 22"')
        commands.append('set xlabel "Time [s]" offset 0,-1.1 font ", 22"')

        commands.append('set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"')
        commands.append('set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"')

        plot_written = False
        colors_index = 0
        index = 0
        count = len(self.plots) - 1
        for plot in self.plots:
            color = colors[colors_index]
            plot_string = ''
            if plot_written == False:
                plot_string = 'plot '
                plot_written = True

            plot_string = plot_string + "'" + plot["qmd_csv"] + "'" + ' using ($0*0.1 + ' + plot["start_delay"] + '):(($1 - path_delay)/1000000) with point pointtype 7 ps 0.3 lc rgb "' + color + '" title "' + plot["qmd_title"] + '"'

            if plot["plot_fec"] == True:
                colors_index = colors_index + 2
            else:
                colors_index = colors_index + 1

            if (index < count):
                plot_string = plot_string + ',' + " \\"
            index = index + 1

            commands.append(plot_string)

        with open(plot_file, 'w') as f:
            for item in commands:
              f.write("%s\n" % item)

        subprocess.check_output('gnuplot ' + plot_file, shell=True)
