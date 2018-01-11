import subprocess
import csv
import matplotlib
import os

matplotlib.use('Agg')


class Plotter:
    @staticmethod
    def __write_bandwidths(bandwidths, target, resolution=10):
        with open(target, 'w') as f:
            writer = csv.writer(f)
            for val in bandwidths:
                for i in range(0, resolution):
                    writer.writerow([val])

    def __init__(self, target_dir='./'):
        self.__target_dir = target_dir
        pass

    def generate(self, test):
        for plot_description in test.get_plot_description():
            plot_type = plot_description.get("type", None)
            if plot_type is "srqmd":
                self.__generate_srqmd(test, plot_description)
            elif plot_type is "aggr":
                self.__generate_aggr(test, plot_description)
        pass

    def __generate_srqmd(self, test, plot_description):
        test_description = test.get_descriptions()
        flow_ids = plot_description.get('flow_ids', [])
        flow_descriptions = list(filter(lambda tdesc: tdesc.get("flow_id", None) in flow_ids, test_description))
        plot_file = self.__target_dir + plot_description.get("filename", "unknown") + ".plot"
        output_file = self.__target_dir + plot_description.get("filename", "unknown") + ".pdf"
        colors = iter(["blue", "0x008c48", "#0xf47d23", "#0x662c91", "#0xa21d21", "#0xb43894"])
        plot_id = plot_description.get('plot_id', None)
        plot_bandwidth = plot_description.get("plot_bandwidth", False)
        bandwidths = plot_description.get("bandwidths", [1])

        pathbw_csv = self.__target_dir + (
            plot_id.replace(' ', '_').lower() + "_" if plot_id else "") + "pathbw.csv"
        if os.path.isfile(pathbw_csv) is False:
            Plotter.__write_bandwidths(bandwidths, pathbw_csv)

        sr_range = max(bandwidths) + (1000 if plot_bandwidth else 0)
        commands = []
        commands.append('time=system("date +%Y_%m_%d_%H_%M_%S")')

        commands.append('path_delay=' + str(int(test.latency) * 1000))
        commands.append("output_file='" + output_file + "'")

        commands.append('duration=' + str(test.duration))

        commands.append('font_size=18')

        commands.append('set terminal pdf enhanced rounded size 18,6')
        commands.append('set output output_file')
        commands.append('set datafile separator ","')

        commands.append('set multiplot layout 2, 1 font ",18"')
        commands.append('set tmargin 4')
        commands.append('set bmargin 5')
        commands.append('set lmargin 20')
        commands.append('set rmargin 10')

        commands.append('set key font ",18"')
        commands.append('set key box opaque height 1')
        commands.append('set ytics font ",26"')

        commands.append('set yrange [0:' + str(sr_range) + ']')
        commands.append('set ytics 1000')
        commands.append('set xrange [0:duration]')

        if int(test.duration) < 150:
            commands.append('set xtics 10 offset 0,-1')
        elif int(test.duration) < 350:
            commands.append('set xtics 30 offset 0,-1')

        commands.append('set format x " "')
        commands.append('unset xlabel')

        commands.append('set ylabel "Throughput [kbps]" offset -7,0 font ", 22"')

        commands.append('set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"')
        commands.append('set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"')

        sr_plot_written = False
        qmd_plot_written = False
        sr_plot = []
        qmd_plot = []
        count = len(flow_descriptions)
        actual = 0
        for flow_description in flow_descriptions:
            actual += 1
            last_one = actual == count
            flow = flow_description.get('flow', {})
            evaluations = flow_description.get('evaluations', {})
            sr_csv = evaluations.get('sr', None)
            tcpstat_csv = evaluations.get('tcpstat', None)
            qmd_csv = evaluations.get('qmd', None)
            start_delay = str(flow.start_delay)
            title = flow_description.get("title", "Unknown")
            fec_title = flow_description.get("fec_title", "Unknown")
            plot_fec = flow_description.get("plot_fec", False)

            flow_color = next(colors)

            if sr_csv is not None:
                line = 'plot' if sr_plot_written is False else '\t'
                line += ' \'' + sr_csv + '\' using ($0*0.1 + ' + start_delay + '):(' + (
                '($1+$2)' if plot_fec is False else '$1') + '/125)'
                line += ' with point pointtype 7 ps 0.3'
                line += ' lc rgb "' + flow_color + '" title "' + title + '"'
                line += ", \\" if last_one is False or plot_bandwidth is True or \
                                  tcpstat_csv is not None or plot_fec is True else ""
                sr_plot.append(line)
                sr_plot_written = True

            if plot_fec is True:
                line = '\t\'' + sr_csv + '\' using ($0*0.1 + '
                line += start_delay + '):(($1+$2)/125) with point pointtype 7 ps 0.3 lc rgb "'
                line += next(colors) + '" title "' + fec_title + '"'
                line += ", \\" if last_one is False or plot_bandwidth is True or tcpstat_csv is not None else ""
                sr_plot.append(line)

            if tcpstat_csv is not None:
                sr_plot.append('\t\'' + tcpstat_csv + \
                               '\' using ($0*0.1):(($1+$2)/125) with point pointtype 7 ps 0.3 lc rgb "' + \
                               next(colors) + '" title "' + title + '"' + \
                               (", \\" if last_one is False or plot_bandwidth is True else ""))

            if last_one and plot_bandwidth:
                sr_plot.append(
                    '\t\'' + pathbw_csv + '\' using ($0*0.1):1 with lines lc rgb "0xDC143C" title "Path Capacity"')

            if qmd_csv is not None:
                line = 'plot' if qmd_plot_written is False else '\t'
                line += ' \'' + qmd_csv + '\' using ($0*0.1 + ' + start_delay + '):(($1 - path_delay)/1000000)'
                line += ' with point pointtype 7 ps 0.3 lc'
                line += ' rgb "' + flow_color + '" title "' + title + '"' + (", \\" if last_one is False else "")
                qmd_plot.append(line)
                qmd_plot_written = True

        commands.append('set title "' + plot_description.get('sr_title', "Sending Rate") + '" font "sans, 24"')
        commands.extend(sr_plot)

        commands.append('set title "' + plot_description.get('qmd_title', "Queue Delay") + '"  font "sans, 26"')
        commands.append('set yrange [0:0.5]')
        commands.append('set ytics 0.25')
        commands.append('set xrange [0:duration]')
        if int(test.duration) < 150:
            commands.append('set xtics 10 offset 0,-0.5')
        elif int(test.duration) < 350:
            commands.append('set xtics 30 offset 0,-0.5')

        commands.append('set format x "%3.0f"')
        commands.append('set xtics font ", 26"')

        commands.append('set ylabel "Queue Delay [s]" offset -7,0 font ", 22"')
        commands.append('set xlabel "Time [s]" offset 0,-1.1 font ", 22"')

        commands.append('set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"')
        commands.append('set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"')

        commands.extend(qmd_plot)

        with open(plot_file, 'w') as f:
            for item in commands:
                f.write("%s\n" % item)
        subprocess.check_output('gnuplot ' + plot_file, shell=True)

    def __generate_aggr(self, test, plot_description):
        test_description = test.get_descriptions()
        flow_ids = plot_description.get('flow_ids', [])
        flow_descriptions = list(filter(lambda tdesc: tdesc.get("flow_id", None) in flow_ids, test_description))
        filename = plot_description.get("filename", "unknown")
        plot_file = self.__target_dir + filename + ".plot"
        output_file = self.__target_dir + filename + ".pdf"
        colors = iter(["blue", "0x008c48", "#0xf47d23", "#0x662c91", "#0xa21d21", "#0xb43894"])
        plot_id = plot_description.get('plot_id', None)
        plot_bandwidth = plot_description.get("plot_bandwidth", False)
        bandwidths = plot_description.get("bandwidths", [1])

        pathbw_csv = self.__target_dir + (
            plot_id.replace(' ', '_').lower() + "_" if plot_id else "") + "pathbw.csv"
        if os.path.isfile(pathbw_csv) is False:
            Plotter.__write_bandwidths(bandwidths, pathbw_csv)

        sr_range = max(bandwidths) + (1000 if plot_bandwidth else 0)
        commands = []
        commands.append('time=system("date +%Y_%m_%d_%H_%M_%S")')

        commands.append('path_delay=' + str(int(test.latency) * 1000))
        commands.append("output_file='" + output_file + "'")

        commands.append('duration=' + str(test.duration))

        commands.append('font_size=18')

        commands.append('set terminal pdf enhanced rounded size 18,6')
        commands.append('set output output_file')
        commands.append('set datafile separator ","')

        commands.append('set multiplot layout 2, 1 font ",18"')
        commands.append('set tmargin 4')
        commands.append('set bmargin 5')
        commands.append('set lmargin 20')
        commands.append('set rmargin 10')

        commands.append('set key font ",18"')
        commands.append('set key box opaque height 1')
        commands.append('set ytics font ",26"')

        commands.append('set yrange [0:' + str(sr_range) + ']')
        commands.append('set ytics 1000')
        commands.append('set xrange [0:duration]')

        if int(test.duration) < 150:
            commands.append('set xtics 10 offset 0,-1')
        elif int(test.duration) < 350:
            commands.append('set xtics 30 offset 0,-1')

        commands.append('set format x " "')
        commands.append('unset xlabel')

        commands.append('set ylabel "Throughput [kbps]" offset -7,0 font ", 22"')

        commands.append('set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"')
        commands.append('set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"')

        sr_plot_written = False
        ratio_plot_written = False
        sr_plot = []
        ratio_plot = []
        count = len(flow_descriptions)
        actual = 0

        title = plot_description.get("title", "Unknown")
        fec_title = plot_description.get("fec_title", "Unknown")
        plot_fec = plot_description.get("plot_fec", False)
        tcp_title = plot_description.get("tcp_title", "Unknown")
        sr_csv_candidates = []
        tcp_csv_candidates = []
        qmd_csv = None
        for flow_description in flow_descriptions:
            actual += 1
            last_one = actual == count
            flow = flow_description.get('flow', {})
            evaluations = flow_description.get('evaluations', {})
            sr_csv = evaluations.get('sr', None)
            tcpstat_csv = evaluations.get('tcpstat', None)
            start_delay = str(flow.start_delay)

            sr_csv_candidates.append((sr_csv, start_delay))
            tcp_csv_candidates.append(tcpstat_csv)

        flow_color = next(colors)

        aggr_sending_rates = []
        all_sending_rates = []
        subflows_num = 0
        for csv_tuple in sr_csv_candidates:
            subflows_num += 1
            start_delay = csv_tuple[1]
            index = int(start_delay) * 10
            if len(aggr_sending_rates) < index:
                aggr_len = len(aggr_sending_rates)
                aggr_sending_rates.extend([(0, 0)] * (index - aggr_len))
                for i in range(index - aggr_len):
                    all_sending_rates.append([0])
            csv_len = 0
            with open(csv_tuple[0]) as csvfile:
                readCSV = csv.reader(csvfile, delimiter=',')
                csv_len = sum(1 for row in readCSV)
            with open(csv_tuple[0]) as csvfile:
                readCSV = csv.reader(csvfile, delimiter=',')
                if len(aggr_sending_rates) < index + csv_len:
                    aggr_len = len(aggr_sending_rates)
                    aggr_sending_rates.extend([(0, 0)] * (index + csv_len - aggr_len))
                    for i in range(index + csv_len - aggr_len):
                        all_sending_rates.append([0])
                for row in readCSV:
                    sr_tuple = aggr_sending_rates[index]
                    aggr_sending_rates[index] = (sr_tuple[0] + int(row[0]), sr_tuple[1] + int(row[1]))
                    ratio_item = int(row[0]) + int(row[1])
                    all_sending_rates[index][0] = sr_tuple[0] + sr_tuple[1] + ratio_item
                    all_sending_rates[index].append(ratio_item)
                    index += 1

        sr_csv = self.__target_dir + filename + "_aggr_sr.csv"
        with open(sr_csv, "w") as f:
            writer = csv.writer(f)
            for sending_rate in aggr_sending_rates:
                # print(sending_rate)
                writer.writerow([sending_rate[0], sending_rate[1]])
            # writer.writerows(sending_rates)

        ratio_csv = self.__target_dir + filename + "_ratio_sr.csv"
        with open(ratio_csv, "w") as f:
            writer = csv.writer(f)
            for sending_rate in all_sending_rates:
                # print(sending_rate)
                writer.writerow(sending_rate)

        tcpstat_csv = None

        if sr_csv is not None:
            line = 'plot' if sr_plot_written is False else '\t'
            line += ' \'' + sr_csv + '\' using ($0*0.1):(' + (
                '($1+$2)' if plot_fec is False else '$1') + '/125)'
            line += ' with point pointtype 7 ps 0.3'
            line += ' lc rgb "' + flow_color + '" title "' + title + '"'
            line += ", \\" if plot_bandwidth is True or \
                              tcpstat_csv is not None or plot_fec is True else ""
            sr_plot.append(line)

        if plot_fec is True:
            line = '\t\'' + sr_csv + '\' using ($0*0.1)'
            line += ':(($1+$2)/125) with point pointtype 7 ps 0.3 lc rgb "'
            line += next(colors) + '" title "' + fec_title + '"'
            line += ", \\" if plot_bandwidth is True or tcpstat_csv is not None else ""
            sr_plot.append(line)

        if tcpstat_csv is not None:
            sr_plot.append('\t\'' + tcpstat_csv + \
                           '\' using ($0*0.1):(($1+$2)/125) with point pointtype 7 ps 0.3 lc rgb "' + \
                           next(colors) + '" title "' + tcp_title + '"' + \
                           (", \\" if plot_bandwidth is True else ""))

        if plot_bandwidth:
            sr_plot.append(
                '\t\'' + pathbw_csv + '\' using ($0*0.1):1 with lines lc rgb "0xDC143C" title "Path Capacity"')

        if ratio_csv is not None:
            for subflow_num in range(subflows_num):
                line = 'plot' if ratio_plot_written is False else '\t'
                ratio_plot_written = True
                dollar_str = "("
                for i in range(subflow_num + 1):
                    dollar_str += "$" + str(i + 2) + ("+" if i < subflow_num else "")
                dollar_str += ")"
                line += ' \'' + ratio_csv + '\' using ($0*0.1):((' + dollar_str + '/$1)*100)'
                line += ' with point pointtype 7 ps 0.3 lc'
                line += ' rgb "' + flow_color + '" title "Subflow ' + str(subflow_num + 1) + '"' + (', \\' if subflow_num < subflows_num - 1 else '')
                flow_color = next(colors)
                ratio_plot.append(line)

        commands.append('set title "' + plot_description.get('sr_title', "Sending Rate") + '" font "sans, 24"')
        commands.extend(sr_plot)

        commands.append('set title "' + plot_description.get('qmd_title', "Ratios between subflow") + '"  font "sans, 26"')
        commands.append('set yrange [0:100]')
        commands.append('set ytics 25')
        commands.append('set xrange [0:duration]')
        if int(test.duration) < 150:
            commands.append('set xtics 10 offset 0,-0.5')
        elif int(test.duration) < 350:
            commands.append('set xtics 30 offset 0,-0.5')

        commands.append('set format x "%3.0f"')
        commands.append('set xtics font ", 26"')

        commands.append('set ylabel "Ratio [%]" offset -7,0 font ", 22"')
        commands.append('set xlabel "Time [s]" offset 0,-1.1 font ", 22"')

        commands.append('set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"')
        commands.append('set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"')

        commands.extend(ratio_plot)

        with open(plot_file, 'w') as f:
            for item in commands:
                f.write("%s\n" % item)
        subprocess.check_output('gnuplot ' + plot_file, shell=True)
