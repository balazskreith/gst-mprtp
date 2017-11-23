from constants import *
from flows import *
from testbedctrler import *
from midboxctrler import *
from flowsctrler import *
from pathconfig import *
from pathctrler import *
from testctrler import *
from collections import deque
from tests import *
from testctrlerbuilder import *
from time import sleep
from evaluator import *
from time import sleep
import gzip

import threading

import logging
import sys
import os
import subprocess

import signal
import sys
import argparse

from plotter import *


class Tester:
    def __init__(self):
        signal.signal(signal.SIGINT, self.on_exit_signal)
        signal.signal(signal.SIGTERM, self.on_exit_signal)
        self.__target_dir = None
        self.__test = None
        self.__test_ctrler = None
        self.__exit_signal_hit = 0
        self.__duration = 0

    def on_exit_signal(self, signum, frame):
        if (0 < self.__exit_signal_hit):
            sys.exit(0)
            return
        self.__exit_signal_hit = self.__exit_signal_hit + 1
        self.__stop_test(self.__test)
        sys.exit(0)


    def setup(self, algorithm, params):
        self.__target_dir = params.target
        self.__test = self.__make_test(params.type, algorithm, params.latency, params.jitter, params.source, params.sink)
        self.__plotter = None
        self.__evaluator = None

        self.__test.add_evaluator(self.__evaluator)
        self.__evaluator.add_plotter(self.__plotter)

        self.__test_ctrler = TestCtrlerBuilder.make(self.__test)
        self.__duration = self.__test.duration + self.__test_ctrler.get_max_source_to_sink_delay() + 2

    def start_statsrelayer(self):
        # cmdin = '/tmp/statsrelayer.cmd.in'
        # subprocess.call("unlink " + cmdin, shell = True)
        # subprocess.call("mkfifo " + cmdin, shell = True)
        # sleep(1)
        subprocess.call("./statsrelayer.out -d &", shell = True)

    def start(self):
        for i in range(0,args.runs):
            subprocess.call("./statsrelayer.out -d &", shell = True)
            # threading.Thread(target=self.start_statsrelayer).start()
            sleep(1)
            threading.Thread(target=self.__test_ctrler.start).start()
            sleep(self.__duration)
            self.__stop_test(self.__test)

    def __stop_test(self, test):
        self.__test_ctrler.stop()
        command = 'sudo pkill tcpdump'
        subprocess.call(command, shell = True)

        command = 'sudo pkill --signal SIGTERM statsrelayer'
        # Statsrelayer stop
        # command = 'echo "fls *;ext;" >> /tmp/statsrelayer.cmd.in'
        subprocess.call(command, shell = True)
        sleep(3)

        if not os.path.isdir(self.__target_dir):
            os.makedirs(self.__target_dir)
        else:
            folder = self.__target_dir
            for the_file in os.listdir(folder):
                file_path = os.path.join(folder, the_file)
                try:
                    if os.path.isfile(file_path):
                        os.unlink(file_path)
                    #elif os.path.isdir(file_path): shutil.rmtree(file_path)
                except Exception as e:
                    print(e)

        for saved_file in self.__test.get_saved_files():
            os.rename(saved_file, self.__target_dir + saved_file)

        evaluator_params = self.__test.get_forward_evaluator_params
        # evaluator_params = self.__test.get_backward_evaluator_params
        params_num = 0
        for evaluator_params in [self.__test.get_forward_evaluator_params, self.__test.get_backward_evaluator_params]:
            temp = 0
            for params in evaluator_params():
                temp = temp + 1
            if (temp < 1):
                continue
            params_num = params_num + 1

        separated_plots = 1 < params_num
        separations = ["forward", "backward"]
        evaluator_param_index = 0
        bandwidths_getter = [self.__test.get_forward_bandwidths, self.__test.get_backward_bandwidths]

        for evaluator_params in [self.__test.get_forward_evaluator_params, self.__test.get_backward_evaluator_params]:
            temp = 0
            for params in evaluator_params():
                temp = temp + 1
            if (temp < 1):
                continue

            pathbw_csv = ""
            sr_qmd_plot_pdf = self.__target_dir + "_".join([test.name, test.latency + 'ms', test.jitter + 'ms']) + '.pdf'
            if (separated_plots):
                sr_qmd_plot_pdf = self.__target_dir + "_".join([test.name, test.latency + 'ms', test.jitter + 'ms', separations[evaluator_param_index]]) + '.pdf'
            plots = []
            plot_indexes = False
            plot_index = 0
            for params in evaluator_params():
                plot_index = plot_index + 1

            plot_indexes = True if 1 < plot_index else False
            plot_index = 1
            for params in evaluator_params():
                bandwidths = []
                if (evaluator_param_index == 0):
                    bandwidths = self.__test.get_forward_bandwidths(resolution = 10)
                else:
                    bandwidths = self.__test.get_backward_bandwidths(resolution = 10)

                evaluator = None
                if (self.__test.multipath):
                    evaluator = MPEvaluator(target_dir = self.__target_dir,
                        snd_path = self.__target_dir + params["snd_log"],
                        rcv_path = self.__target_dir + params["rcv_log"],
                        ply_path = self.__target_dir + params["ply_log"],
                        tcp_path = (self.__target_dir + params["tcp_log"]) if params["tcp_log"] != None else None,
                        bandwidths = bandwidths)
                else:
                    evaluator = Evaluator(target_dir = self.__target_dir,
                        snd_path = self.__target_dir + params["snd_log"],
                        rcv_path = self.__target_dir + params["rcv_log"],
                        ply_path = self.__target_dir + params["ply_log"],
                        tcp_path = (self.__target_dir + params["tcp_log"]) if params["tcp_log"] != None else None,
                        bandwidths = bandwidths)
                evaluations = evaluator.perform()
                pathbw_csv = evaluations["pathbw_csv"]
                sr_title = "Sending Rate"
                qmd_title = "Path Delay"
                fec_title = "Sending Rate + FEC rate"
                tcp_title = "TCP Rate"
                if (plot_indexes):
                    subindex = " " + str(plot_index)
                    sr_title = sr_title + subindex
                    qmd_title = qmd_title + subindex
                    fec_title = fec_title + subindex
                    plot_index = plot_index + 1

                plots.append({
                    "sr_csv": evaluations["sr_csv"],
                    "qmd_csv": evaluations["qmd_csv"],
                    "sr_title": sr_title,
                    "qmd_title": qmd_title,
                    "plot_fec": test.plot_fec,
                    "fec_title": fec_title,
                    "start_delay": params["start_delay"],
                    "tcp_csv": evaluations["tcp_csv"],
                    "tcp_title": tcp_title
                })

            sr_qmd_plotter = None
            if (self.__test.multipath):
                sr_qmd_plotter = MPSrQmdGnuPlot(pathbw_csv = pathbw_csv,
                    path_delay = test.latency, output_file = sr_qmd_plot_pdf, duration = test.duration)
            else:
                sr_qmd_plotter = SrQmdGnuPlot(pathbw_csv = pathbw_csv,
                    path_delay = test.latency, output_file = sr_qmd_plot_pdf, duration = test.duration)

            for plot in plots:
                sr_qmd_plotter.add_plot(sr_csv = plot["sr_csv"], qmd_csv = plot["qmd_csv"],
                    sr_title = plot["sr_title"], qmd_title = plot["qmd_title"], plot_fec = plot["plot_fec"],
                    fec_title = plot["fec_title"], start_delay = plot["start_delay"], tcp_csv = plot["tcp_csv"],
                    tcp_title = plot["tcp_title"])
            sr_qmd_plotter.generate()

            evaluator_param_index = evaluator_param_index + 1

        for file_to_compress in self.__test.get_file_to_compress():
            content = None
            with open(self.__target_dir + file_to_compress, 'rb') as f:
                content = f.read()
            with gzip.open(self.__target_dir + file_to_compress + '.gz', 'wb') as f:
                f.write(content)
            os.remove(self.__target_dir + file_to_compress)



    def __make_test(self, type, algorithm, latencies, jitters, source_type, sink_type):
        result = None
        if (type == "rmcat1"):
            result = RMCAT1(
              algorithm = algorithm,
              latency=latencies[0],
              jitter=jitters[0],
              source_type=source_type,
              sink_type=sink_type
            )
        elif (type == "rmcat2"):
            result = RMCAT2(
              algorithm = algorithm,
              latency=latencies[0],
              jitter=jitters[0],
              source_type=source_type,
              sink_type=sink_type
            )
        elif (type == "rmcat3"):
            result = RMCAT3(
              algorithm = algorithm,
              latency=latencies[0],
              jitter=jitters[0],
              source_type=source_type,
              sink_type=sink_type
            )
        elif (type == "rmcat4"):
            result = RMCAT4(
              algorithm = algorithm,
              latency=latencies[0],
              jitter=jitters[0],
              source_type=source_type,
              sink_type=sink_type
            )
        elif (type == "rmcat6"):
            result = RMCAT6(
              algorithm = algorithm,
              latency=latencies[0],
              jitter=jitters[0],
              source_type=source_type,
              sink_type=sink_type
            )
        elif (type == "rmcat7"):
            result = RMCAT7(
              algorithm = algorithm,
              latency=latencies[0],
              jitter=jitters[0],
              source_type=source_type,
              sink_type=sink_type
            )
        elif (type == "mprtp1"):
            result = MPRTP1(
              algorithm = algorithm,
              latency=latencies[0],
              jitter=jitters[0],
              source_type=source_type,
              sink_type=sink_type
            )
        return result

parser = argparse.ArgumentParser()
parser.add_argument("type", help="The latency of the path", choices=['rmcat1', 'rmcat2', 'rmcat3', 'rmcat4', 'rmcat5', 'rmcat6', 'rmcat7',
    'mprtp1', 'mprtp2', 'mprtp3', 'mprtp4', 'mprtp5', 'mprtp6', 'mprtp7'])
parser.add_argument("-l", "--latency", help="The latency of the path", type=int, nargs='+', choices=[50, 100, 150, 300], default=[50])
parser.add_argument("-j", "--jitter", help="The jitter for the test", type=int, nargs='+', default=[0])
parser.add_argument("-a", "--algorithm", help="The algorithm for the test", default="FRACTaL", choices=["FRACTaL", "SCReAM"])
parser.add_argument("-r", "--runs", help="The runtimes", type=int, default=1)
parser.add_argument("-t", "--target", help="The target directory", default="temp/")
parser.add_argument("-s", "--source", help="The source type format", default="FILE:foreman_cif.yuv:1:352:288:2:25/1")
parser.add_argument("-i", "--sink", help="The sink type format", default="FAKESINK")
args = parser.parse_args()

root = logging.getLogger()
root.setLevel(logging.DEBUG)


ch = logging.StreamHandler(sys.stdout)
ch.setLevel(logging.DEBUG)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
ch.setFormatter(formatter)
root.addHandler(ch)
algorithm = Algorithms.FRACTaL
if (args.algorithm == "SCReAM"):
    algorithm = Algorithms.SCReAM

print('Run test %s, algorithm: %s by %d times with [%s] path latency and jitter [%s]' % (args.type, str(algorithm.name), args.runs, ", ".join(map(str, args.latency)),
    ", ".join(map(str, args.jitter))))

tester = Tester()
tester.setup(algorithm, args)
tester.start()
