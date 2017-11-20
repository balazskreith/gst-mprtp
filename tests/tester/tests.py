from constants import *
from flows import *
from testbedctrler import *
from midboxctrler import *
from flowsctrler import *
from pathconfig import *
from pathctrler import *
from testctrler import *
from collections import deque

import numpy as np

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
        self.__forward_path_bandwidths = []
        self.__backward_path_bandwidths = []
        self.__multipath = False

    def set_multipath(self, value):
        self.__multipath = value

    @property
    def multipath(self):
        return self.__multipath

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

    def get_file_to_compress(self):
        return []

    @property
    def duration(self):
        """Gets the duration of the test in seconds"""
        return self.__duration

    def get_bandwidths_for(self, bandwidths = [], resolution = 1):
        """Private method to get the bandwidths of the test in seconds"""
        if (resolution <= 1):
            return bandwidths

        result = []
        for bandwidth in bandwidths:
            result.extend([bandwidth] * resolution)
        return result

    def get_forward_bandwidths(self, resolution = 1):
        """Gets the bandwidths of the test in seconds"""
        return self.get_bandwidths_for(bandwidths = self.__forward_path_bandwidths, resolution = resolution)

    def get_backward_bandwidths(self, resolution = 1):
        """Gets the bandwidths of the test in seconds"""
        return self.get_bandwidths_for(bandwidths = self.__backward_path_bandwidths, resolution = resolution)

    @property
    def algorithm(self):
        """Gets the algorithm of the test"""
        return self.__algorithm

    @property
    def name(self):
        """Gets the name of the test"""
        return self.__name

    def make_path_stage(self, stages_deque, bandwidths):
        """
        Private method making path stages
        Parameters:
        -----------
        stages_deque : deque
            contains all path stages in a queue needs to be placed in a chain of path stage
        bandwidths : list
            The list contains the number of seconds for keeping the limit
        """
        if len(stages_deque) == 0:
            return None
        stage = stages_deque.popleft()
        duration = stage["duration"]
        path_config = stage["config"]
        bandwidths.extend([path_config.bandwidth] * duration)
        result = PathStage(duration = duration, path_config = path_config, next_stage = self.make_path_stage(stages_deque, bandwidths))
        return result

    def make_forward_path_stage(self,stages_deque):
        """
        Private method making path stages
        Parameters:
        -----------
        stages_deque : deque
            contains all path stages in a queue needs to be placed in a chain of path stage
        """
        return self.make_path_stage(stages_deque, self.__forward_path_bandwidths)

    def make_backward_path_stage(self,stages_deque):
        """
        Private method making path stages
        Parameters:
        -----------
        stages_deque : deque
            contains all path stages in a queue needs to be placed in a chain of path stage
        """
        return self.make_path_stage(stages_deque, self.__backward_path_bandwidths)

    def get_flows(self):
        """"Gets the flows for the test"""
        return []

    def get_path_ctrlers(self):
        """"Gets the path controllers"""
        return []

    def get_saved_files(self):
        """"Gets the saved files"""
        return []

    def get_forward_evaluator_params(self):
        """"Gets the saved files"""
        return []

    def get_backward_evaluator_params(self):
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

    def get_forward_evaluator_params(self):
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

        # return make_path_ctrler(path_name="veth2", path_stage = make_forward_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_forward_path_stage(deque(stages)))
        result = [path_ctrler]
        return result

    @property
    def plot_fec(self):
        return True

class RMCAT2(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat2", 125, algorithm, str(latency), str(jitter))

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

    def get_forward_evaluator_params(self):
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
            "duration": 25,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 25,
            "config" : PathConfig(bandwidth = 1000, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 25,
            "config" : PathConfig(bandwidth = 1750, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 25,
            "config" : PathConfig(bandwidth = 500, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 25,
            "config" : PathConfig(bandwidth = 1000, latency = self.__latency, jitter = self.__jitter)
        }]

        # return make_path_ctrler(path_name="veth2", path_stage = make_forward_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_forward_path_stage(deque(stages)))
        result = [path_ctrler]
        return result

class RMCAT3(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat3", 100, algorithm, str(latency), str(jitter))

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
            rtp_ip="10.0.0.1",
            rtp_port=5002,
            rtcp_ip="10.0.0.6",
            rtcp_port=5003,
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            flipped = True)
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

    def get_forward_evaluator_params(self):
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

    def get_backward_evaluator_params(self):
        """"Gets the saved files"""
        index = 0
        start_delays = ["0"]
        for flownum in ["_2"]:
            yield {
                "snd_log": "snd_packets" + flownum + ".csv",
                "rcv_log": "rcv_packets" + flownum + ".csv",
                "ply_log": "ply_packets" + flownum + ".csv",
                "tcp_log": None,
                "start_delay": start_delays[index]
            }
            index = index + 1

    def get_path_ctrlers(self):
        forward_stages = [
        {
            "duration": 20,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 20,
            "config" : PathConfig(bandwidth = 1000, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 20,
            "config" : PathConfig(bandwidth = 500, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 20,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        }]

        backward_stages = [
        {
            "duration": 35,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 35,
            "config" : PathConfig(bandwidth = 800, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 35,
            "config" : PathConfig(bandwidth = 1000, latency = self.__latency, jitter = self.__jitter)
        }]

        # return make_path_ctrler(path_name="veth2", path_stage = make_forward_path_stage(stages))
        forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_forward_path_stage(deque(forward_stages)))
        backward_path_ctrler = PathShellCtrler(path_name="veth3", path_stage = self.make_backward_path_stage(deque(backward_stages)))
        result = [forward_path_ctrler, backward_path_ctrler]
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

    def get_forward_evaluator_params(self):
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

        # return make_path_ctrler(path_name="veth2", path_stage = make_forward_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_forward_path_stage(deque(stages)))
        result = [path_ctrler]
        return result

class RMCAT5(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat4", 300, algorithm, str(latency), str(jitter))

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

    def get_forward_evaluator_params(self):
        """"Gets the saved files"""
        index = 0
        start_delays = ["0", "20", "40"]
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

        # return make_path_ctrler(path_name="veth2", path_stage = make_forward_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_forward_path_stage(deque(stages)))
        result = [path_ctrler]
        return result

class RMCAT6(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat6", 120, algorithm, str(latency), str(jitter))

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
            start_delay = 3,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)

        tcp_flow = TCPFlow(name = "TCPLong flow", server_ip = "10.0.0.6", server_port = "12345", duration = 120)
        result = [rtp_flow_1, tcp_flow]
        return result

    def get_saved_files(self):
        """"Gets the saved files"""
        for rtpflow in ["rtpflow_1"]:
            for participant in ["snd", "rcv"]:
                yield rtpflow + "-" + participant + ".log"
        for flownum in ["_1"]:
            for participant in ["snd", "rcv", "ply"]:
                yield participant + "_packets" + flownum + ".csv"
        yield "tcp_packets_1.pcap"

    def get_forward_evaluator_params(self):
        """"Gets the saved files"""
        index = 0
        start_delays = ["5"]
        for flownum in ["_1"]:
            yield {
                "snd_log": "snd_packets" + flownum + ".csv",
                "rcv_log": "rcv_packets" + flownum + ".csv",
                "ply_log": "ply_packets" + flownum + ".csv",
                "tcp_log": "tcp_packets" + flownum + ".pcap",
                "start_delay": start_delays[index]
            }
            index = index + 1

    def get_path_ctrlers(self):
        stages = [
        {
            "duration": 120,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        },
        ]

        # return make_path_ctrler(path_name="veth2", path_stage = make_forward_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_forward_path_stage(deque(stages)))
        tcp_pcap_listener = PathPcapListener(network_type = "tcp", network_interface = "veth2", log_path = "tcp_packets_1.pcap")
        result = [path_ctrler, tcp_pcap_listener]
        return result

    def get_file_to_compress(self):
        return ["tcp_packets_1.pcap"]

class RMCAT7(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat7", 300, algorithm, str(latency), str(jitter))

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
            start_delay = 3,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)

        tcp_server = TCPFlow(name = "TCPLong flow", server_ip = "10.0.0.6", server_port = "12345", duration = 300, create_client = False)
        result = [rtp_flow_1, tcp_server]
        start_time = 0
        while(start_time < 150):
            print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!")
            end = int(np.random.uniform(low = 1.0, high = 5.0))
            print (" end: " + str(end))
            start_time_plus = 0
            for x in range(0,end):
                print ("!!x!!: " + str(x) + " end: " + str(end))
                duration = int(np.random.exponential(scale = 20.0)) + 1
                tcp_flow = TCPFlow(name = "TCPLong flow", server_ip = None, server_port = None, tcp_server = tcp_server.tcp_server, start_delay = start_time,
                    duration = duration)
                start_time_plus = duration if start_time_plus < duration else start_time_plus
                start_time += int(np.random.uniform(low = 0, high = duration / 2)) + 1
                result.append(tcp_flow)

            # start_time += int(np.random.uniform(low = start_time_plus / 2, high = start_time_plus))
            print ("!!!!!! start time:" + str(start_time))
        return result

    def get_saved_files(self):
        """"Gets the saved files"""
        for rtpflow in ["rtpflow_1"]:
            for participant in ["snd", "rcv"]:
                yield rtpflow + "-" + participant + ".log"
        for flownum in ["_1"]:
            for participant in ["snd", "rcv", "ply"]:
                yield participant + "_packets" + flownum + ".csv"
        yield "tcp_packets_1.pcap"

    def get_forward_evaluator_params(self):
        """"Gets the saved files"""
        index = 0
        start_delays = ["5"]
        for flownum in ["_1"]:
            yield {
                "snd_log": "snd_packets" + flownum + ".csv",
                "rcv_log": "rcv_packets" + flownum + ".csv",
                "ply_log": "ply_packets" + flownum + ".csv",
                "tcp_log": "tcp_packets" + flownum + ".pcap",
                "start_delay": start_delays[index]
            }
            index = index + 1

    def get_path_ctrlers(self):
        stages = [
        {
            "duration": 300,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        },
        ]

        # return make_path_ctrler(path_name="veth2", path_stage = make_forward_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_forward_path_stage(deque(stages)))
        tcp_pcap_listener = PathPcapListener(network_type = "tcp", network_interface = "veth2", log_path = "tcp_packets_1.pcap")
        result = [path_ctrler, tcp_pcap_listener]
        return result

    def get_file_to_compress(self):
        return ["tcp_packets_1.pcap"]


class MPRTP1(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "mprtp1", 125, algorithm, str(latency), str(jitter))

        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

        self.set_multipath(True)

    def get_flows(self):
        mprtp_flow_1 = MPRTPFlow(name="mprtpflow_1",
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=["10.0.0.6", "10.1.1.6"],
            rtp_ports=[5000, 5002],
            rtcp_ips=["10.0.0.1", "10.1.1.1"],
            rtcp_ports=[5001, 5003],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id)
        result = [mprtp_flow_1]
        return result

    def get_saved_files(self):
        """"Gets the saved files"""
        for rtpflow in ["rtpflow_1", "rtpflow_2"]:
            for participant in ["snd", "rcv"]:
                yield rtpflow + "-" + participant + ".log"
        for flownum in ["_1"]:
            for participant in ["snd", "rcv", "ply"]:
                yield participant + "_packets" + flownum + ".csv"

    def get_forward_evaluator_params(self):
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
            "duration": 25,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 25,
            "config" : PathConfig(bandwidth = 1000, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 25,
            "config" : PathConfig(bandwidth = 1750, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 25,
            "config" : PathConfig(bandwidth = 500, latency = self.__latency, jitter = self.__jitter)
        },
        {
            "duration": 25,
            "config" : PathConfig(bandwidth = 1000, latency = self.__latency, jitter = self.__jitter)
        }]

        # return make_path_ctrler(path_name="veth2", path_stage = make_forward_path_stage(stages))
        path_ctrler = PathShellCtrler(path_name="veth2", path_stage = self.make_forward_path_stage(deque(stages)))
        result = [path_ctrler]
        return result
