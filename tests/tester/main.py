from constants import *
from flows import *
from testbedctrler import *
from midboxctrler import *
from flowsctrler import *
from pathconfig import *
from pathctrler import *
from testctrler import *

def make_path_stage(stages_seq):
    first_path_stage = path_stage = None
    for stage in stages_seq:
        path_stage = PathStage(duration = stage["duration"], path_config = stage["config"], next = path_stage)
        if (first_path_stage == None):
            first_path_stage = path_stage
    return first_path_stage

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
    return PathShellCtrler(path_name="veth2", path_stage = make_path_stage(stages))


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
