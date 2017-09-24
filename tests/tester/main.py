from constants import *
from flows import *
from testbedctrler import *
from midboxctrler import *
from flowsctrler import *
from pathconfig import *
from pathctrler import *
from testctrler import *
from collections import deque

import logging
import sys

root = logging.getLogger()
root.setLevel(logging.DEBUG)

ch = logging.StreamHandler(sys.stdout)
ch.setLevel(logging.DEBUG)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
ch.setFormatter(formatter)
root.addHandler(ch)


def make_path_stage(stages_deque):
    if len(stages_deque) == 0:
        return None
    stage = stages_deque.popleft()
    result = PathStage(duration = stage["duration"], path_config = stage["config"], next_stage = make_path_stage(stages_deque))
    # print(result)
    return result

def make_midbox_ctrler(*path_ctrler):
    result = MidboxCtrler()
    for path_ctrler in path_ctrlers:
        result.add_path_ctrler(path_ctrler)
    return result

def make_rmcat1_pathctrler(latency, jitter):
    stages = [
    {
        "duration": 20,
        "config" : PathConfig(bandwidth = 1000, latency = latency, jitter = jitter)
    },
    {
        "duration": 20,
        "config" : PathConfig(bandwidth = 2800, latency = latency, jitter = jitter)
    },
    {
        "duration": 20,
        "config" : PathConfig(bandwidth = 600, latency = latency, jitter = jitter)
    },
    {
        "duration": 40,
        "config" : PathConfig(bandwidth = 1000, latency = latency, jitter = jitter)
    }]

    # return make_path_ctrler(path_name="veth2", path_stage = make_path_stage(stages))
    return PathShellCtrler(path_name="veth2", path_stage = make_path_stage(deque(stages)))


def make_rmcat1_rtp_flows():
    rtp_flow = RTPFlow("./", Codecs.VP8, Algorithms.FRACTaL, "10.0.0.6", 5000, "10.0.0.1", 5001, start_delay = 0)
    result = [rtp_flow]
    return result


def make_test_ctrler(test_name, latency, jitter):
    tesbed_ctrler = LinuxTestBedCtrler()
    midbox_ctrler = MidboxShellCtrler()
    flows_ctrler = FlowsCtrler()

    if (test_name == 'rmcat1'):
        rtp_flow = make_rmcat1_rtp_flows()
        path_ctrler = make_rmcat1_pathctrler(latency, jitter)

        midbox_ctrler.add_path_ctrlers(path_ctrler)
        flows_ctrler.add_flows(rtp_flow)


    result = TestCtrler(tesbed_ctrler, midbox_ctrler, flows_ctrler)
    return result

test_ctrler = make_test_ctrler('rmcat1', 50, 1)
test_ctrler.start()
