from constants import *
from flows import *
from testbedctrler import *
from midboxctrler import *
from flowsctrler import *
from pathconfig import *
from pathctrler import *
from testctrler import *
from collections import deque

class MyTest(object):
    """
    Represent a test
    """
    def __init__(self, midbox_ctrler, flow_ctrler):
        pass

    def make_path_stage(self,stages_deque):
        """
        Private method making path stages
        Parameters:
        -----------
        stages_deque : deque
            contains all path stages in a queue needs to be placed in a chain of path stage
        """
        if len(stages_deque) == 0:
            return None
        stage = stages_deque.popleft()
        result = PathStage(duration = stage["duration"], path_config = stage["config"], next_stage = self.make_path_stage(stages_deque))
        return result

    def get_flows(self):
        """"Gets the flows for the test"""
        return []

    def get_path_ctrlers(self):
        """"Gets the path controllers"""
        return []

class RMCAT1(MyTest):
    def __init__(self, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        super(MyTest, self).__init__()

        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

    def get_flows(self):
        rtp_flow = RTPFlow(path="./",
            codec=Codecs.VP8,
            algorithm=Algorithms.FRACTaL,
            rtp_ip="10.0.0.6",
            rtp_port=5000,
            rtcp_ip="10.0.0.1",
            rtcp_port=5001,
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        result = [rtp_flow]
        return result

    def get_path_ctrlers(self):
        stages = [
        {
            "duration": 20,
            "config" : PathConfig(bandwidth = 1000, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 20,
            "config" : PathConfig(bandwidth = 2800, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 20,
            "config" : PathConfig(bandwidth = 600, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 40,
            "config" : PathConfig(bandwidth = 1000, latency = self.__latency, jitter = self.__jitter)
        }]

        # return make_path_ctrler(path_name="veth2", path_stage = make_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_path_stage(deque(stages)))
        result = [path_ctrler]
        return result
