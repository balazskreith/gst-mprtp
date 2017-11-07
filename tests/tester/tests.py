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
    def __init__(self, name, duration, algorithm, latency, jitter):
        """
        Private method making path stages
        Parameters:
        -----------
        name : str
            the name of the test
        duration : int
            The duration of the test in seconds
        """
        self.__algorithm = algorithm
        self.__name = name
        self.__duration = duration
        self.__latency = latency
        self.__jitter = jitter
        self.__bandwidths = []

    @property
    def latency(self):
        """Gets the latencies of the test in miliseconds"""
        return self.__latency

    @property
    def jitter(self):
        """Gets the jitters of the test in miliseconds"""
        return self.__jitter

    @property
    def plot_fec(self):
        """Indicate whether the plot should has fec points"""
        return False

    @property
    def duration(self):
        """Gets the duration of the test in seconds"""
        return self.__duration

    def get_bandwidths(self, resolution = 1):
        """Gets the bandwidths of the test in seconds"""
        if (resolution <= 1):
            return self.__bandwidths

        result = []
        for bandwidth in self.__bandwidths:
            result.extend([bandwidth] * resolution)
        return result

    @property
    def algorithm(self):
        """Gets the algorithm of the test"""
        return self.__algorithm

    @property
    def name(self):
        """Gets the name of the test"""
        return self.__name

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
        duration = stage["duration"]
        path_config = stage["config"]
        self.__bandwidths.extend([path_config.bandwidth] * duration)
        result = PathStage(duration = duration, path_config = path_config, next_stage = self.make_path_stage(stages_deque))
        return result

    def get_flows(self):
        """"Gets the flows for the test"""
        return []

    def get_path_ctrlers(self):
        """"Gets the path controllers"""
        return []

    def get_saved_files(self):
        """"Gets the saved files"""
        return []

    def get_evaluator_params(self):
        """"Gets the saved files"""
        return []

class RMCAT1(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat1", 100, algorithm, str(latency), str(jitter))

        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

    def get_flows(self):
        rtp_flow = RTPFlow(name="rtpflow_1",
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
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

    def get_saved_files(self):
        """"Gets the saved files"""
        for rtpflow in ["rtpflow_1"]:
            for participant in ["snd", "rcv"]:
                yield rtpflow + "-" + participant + ".log"
        for flownum in ["_1"]:
            for participant in ["snd", "rcv", "ply"]:
                yield participant + "_packets" + flownum + ".csv"

def get_evaluator_params(self):
    """"Gets the saved files"""
    index = 0
    start_delays = ["0"]
    for flownum in ["_1"]:
        yield {
            "snd_log": "snd_packets" + flownum + ".csv",
            "rcv_log": "rcv_packets" + flownum + ".csv",
            "ply_log": "ply_packets" + flownum + ".csv",
            "tcp_log": None,
            "start_delay": start_delays[index]
        }
        index = index + 1

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

    @property
    def plot_fec(self):
        return True

class RMCAT2(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat2", 120, algorithm, str(latency), str(jitter))

        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

    def get_flows(self):
        rtp_flow_1 = RTPFlow(name="rtpflow_1",
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ip="10.0.0.6",
            rtp_port=5000,
            rtcp_ip="10.0.0.1",
            rtcp_port=5001,
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        rtp_flow_2 = RTPFlow(name="rtpflow_2",
            path="./",
            flownum=2,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ip="10.0.0.6",
            rtp_port=5002,
            rtcp_ip="10.0.0.1",
            rtcp_port=5003,
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        result = [rtp_flow_1, rtp_flow_2]
        return result

    def get_saved_files(self):
        """"Gets the saved files"""
        for rtpflow in ["rtpflow_1", "rtpflow_2"]:
            for participant in ["snd", "rcv"]:
                yield rtpflow + "-" + participant + ".log"
        for flownum in ["_1", "_2"]:
            for participant in ["snd", "rcv", "ply"]:
                yield participant + "_packets" + flownum + ".csv"

    def get_evaluator_params(self):
        """"Gets the saved files"""
        index = 0
        start_delays = ["0", "0"]
        for flownum in ["_1", "_2"]:
            yield {
                "snd_log": "snd_packets" + flownum + ".csv",
                "rcv_log": "rcv_packets" + flownum + ".csv",
                "ply_log": "ply_packets" + flownum + ".csv",
                "tcp_log": None,
                "start_delay": start_delays[index]
            }
            index = index + 1

    def get_path_ctrlers(self):
        stages = [
        {
            "duration": 35,
            "config" : PathConfig(bandwidth = 1000, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 35,
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

class RMCAT4(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat4", 120, algorithm, str(latency), str(jitter))

        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

    def get_flows(self):
        rtp_flow_1 = RTPFlow(name="rtpflow_1",
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ip="10.0.0.6",
            rtp_port=5000,
            rtcp_ip="10.0.0.1",
            rtcp_port=5001,
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        rtp_flow_2 = RTPFlow(name="rtpflow_2",
            path="./",
            flownum=2,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ip="10.0.0.6",
            rtp_port=5002,
            rtcp_ip="10.0.0.1",
            rtcp_port=5003,
            start_delay = 10,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        rtp_flow_3 = RTPFlow(name="rtpflow_3",
            path="./",
            flownum=3,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ip="10.0.0.6",
            rtp_port=5004,
            rtcp_ip="10.0.0.1",
            rtcp_port=5005,
            start_delay = 20,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        result = [rtp_flow_1, rtp_flow_2, rtp_flow_3]
        return result

    def get_saved_files(self):
        """"Gets the saved files"""
        for rtpflow in ["rtpflow_1", "rtpflow_2", "rtpflow_3"]:
            for participant in ["snd", "rcv"]:
                yield rtpflow + "-" + participant + ".log"
        for flownum in ["_1", "_2", "_3"]:
            for participant in ["snd", "rcv", "ply"]:
                yield participant + "_packets" + flownum + ".csv"

    def get_evaluator_params(self):
        """"Gets the saved files"""
        index = 0
        start_delays = ["0", "10", "20"]
        for flownum in ["_1", "_2", "_3"]:
            yield {
                "snd_log": "snd_packets" + flownum + ".csv",
                "rcv_log": "rcv_packets" + flownum + ".csv",
                "ply_log": "ply_packets" + flownum + ".csv",
                "tcp_log": None,
                "start_delay": start_delays[index]
            }
            index = index + 1

    def get_path_ctrlers(self):
        stages = [
        {
            "duration": 120,
            "config" : PathConfig(bandwidth = 3000, latency = self.__latency, jitter = self.__jitter)
        },
        ]

        # return make_path_ctrler(path_name="veth2", path_stage = make_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_path_stage(deque(stages)))
        result = [path_ctrler]
        return result

class RMCAT4(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat4", 120, algorithm, str(latency), str(jitter))

        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

    def get_flows(self):
        rtp_flow_1 = RTPFlow(name="rtpflow_1",
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ip="10.0.0.6",
            rtp_port=5000,
            rtcp_ip="10.0.0.1",
            rtcp_port=5001,
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        rtp_flow_2 = RTPFlow(name="rtpflow_2",
            path="./",
            flownum=2,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ip="10.0.0.6",
            rtp_port=5002,
            rtcp_ip="10.0.0.1",
            rtcp_port=5003,
            start_delay = 10,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        rtp_flow_3 = RTPFlow(name="rtpflow_3",
            path="./",
            flownum=3,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ip="10.0.0.6",
            rtp_port=5004,
            rtcp_ip="10.0.0.1",
            rtcp_port=5005,
            start_delay = 20,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        result = [rtp_flow_1, rtp_flow_2, rtp_flow_3]
        return result

    def get_saved_files(self):
        """"Gets the saved files"""
        for rtpflow in ["rtpflow_1", "rtpflow_2", "rtpflow_3"]:
            for participant in ["snd", "rcv"]:
                yield rtpflow + "-" + participant + ".log"
        for flownum in ["_1", "_2", "_3"]:
            for participant in ["snd", "rcv", "ply"]:
                yield participant + "_packets" + flownum + ".csv"

    def get_evaluator_params(self):
        """"Gets the saved files"""
        index = 0
        start_delays = ["0", "10", "20"]
        for flownum in ["_1", "_2", "_3"]:
            yield {
                "snd_log": "snd_packets" + flownum + ".csv",
                "rcv_log": "rcv_packets" + flownum + ".csv",
                "ply_log": "ply_packets" + flownum + ".csv",
                "tcp_log": None,
                "start_delay": start_delays[index]
            }
            index = index + 1

    def get_path_ctrlers(self):
        stages = [
        {
            "duration": 120,
            "config" : PathConfig(bandwidth = 3000, latency = self.__latency, jitter = self.__jitter)
        },
        ]

        # return make_path_ctrler(path_name="veth2", path_stage = make_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_path_stage(deque(stages)))
        result = [path_ctrler]
        return result
