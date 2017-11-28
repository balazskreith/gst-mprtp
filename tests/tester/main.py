from tests import *
from testctrlerbuilder import *
from evaluator import *
from time import sleep
import gzip

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
        if 0 < self.__exit_signal_hit:
            sys.exit(0)
        self.__exit_signal_hit = self.__exit_signal_hit + 1
        self.__stop_test(self.__test)
        sys.exit(0)


    def setup(self, algorithm, params):
        self.__target_dir = params.target
        self.__test = self.__make_test(params.type, algorithm, params.latency, params.jitter, params.source, params.sink)

        self.__test_ctrler = TestCtrlerBuilder.make(self.__test)
        self.__duration = self.__test.duration + self.__test_ctrler.get_max_source_to_sink_delay() + 2

    def start(self):
        for i in range(0,args.runs):
            subprocess.call("./statsrelayer.out -d &", shell = True)
            sleep(1)
            threading.Thread(target=self.__test_ctrler.start).start()
            sleep(self.__duration)
            self.__stop_test(self.__test)

    def __stop_test(self, test):
        self.__test_ctrler.stop()
        command = 'sudo pkill tcpdump'
        subprocess.call(command, shell=True)

        command = 'sudo pkill --signal SIGTERM statsrelayer'
        # Statsrelayer stop
        # command = 'echo "fls *;ext;" >> /tmp/statsrelayer.cmd.in'
        subprocess.call(command, shell=True)
        sleep(3)
        target_dir = 'temp/'
        evaluator = Evaluator(target_dir=target_dir)
        plotter = Plotter(target_dir=target_dir)
        evaluator.setup(self.__test)
        plotter.generate(self.__test)
        print(self.__test.get_descriptions())




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
