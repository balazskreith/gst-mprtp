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

    def add_evaluator(self, evaluator):
        self.__evaluator = evaluator

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

    def get_plot_description(self):
        return []

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
        path_stage = PathStage(duration = duration, path_config = path_config, next_stage = self.make_path_stage(stages_deque, bandwidths))
        return path_stage

    def make_bandwidths_and_path_stage(self,stages_deque):
        """
        Private method making path stages
        Parameters:
        -----------
        stages_deque : deque
            contains all path stages in a queue needs to be placed in a chain of path stage
        """
        bandwidths = []
        path_stages = self.make_path_stage(stages_deque, bandwidths)
        return bandwidths, path_stages

    def get_flows(self):
        """"Gets the flows for the test"""
        return []

    def get_path_ctrlers(self):
        """"Gets the path controllers"""
        return []

    def get_descriptions(self):
        return None


class RMCAT1(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat1", 100, algorithm, str(latency), str(jitter))

        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

        self.__forward_bandwidths = None
        self.__forward_path_ctrler = None
        self.__forward_flow = None
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_flow = RTPFlow(name="rtpflow_1",
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
        result = [self.__forward_flow]
        return result

    def __generate_path_ctrlers(self):
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
        self.__forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage = path_stage)
        result = [self.__forward_path_ctrler]
        return result

    def __generate_description(self):
        forward_flow = {
            "flow_id": "rtpflow",
            "title": "RTP",
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow,
            "evaluations": None,
            "sources": None,
        }
        return [forward_flow]

    def get_plot_description(self):
        return [{
            "plot_id": "flow",
            "filename": '_'.join([self.name, str(self.algorithm.name), str(self.latency) + "ms", str(self.jitter) + "ms"]),
            "type": "srqmd",
            "flow_ids": ["rtpflow"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": True,
        }]

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description



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

        self.__forward_bandwidths = None
        self.__forward_path_ctrler = None
        self.__forward_flow_1 = None
        self.__forward_flow_2 = None
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_flow_1 = RTPFlow(name="rtpflow_1",
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

        self.__forward_flow_2 = RTPFlow(name="rtpflow_2",
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
        result = [self.__forward_flow_1, self.__forward_flow_2]
        return result

    def __generate_path_ctrlers(self):
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
        self.__forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage = path_stage)
        result = [self.__forward_path_ctrler]
        return result

    def __generate_description(self):
        forward_flow_1 = {
            "flow_id": "rtpflow_1",
            "title": "RTP 1",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_1,
            "evaluations": None,
            "sources": None,
        }

        forward_flow_2 = {
            "flow_id": "rtpflow_2",
            "title": "RTP 2",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_2,
            "evaluations": None,
            "sources": None,
        }
        return [forward_flow_1, forward_flow_2]

    def get_plot_description(self):
        return [{
            "plot_id": "flows",
            "filename": '_'.join([self.name, str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "type": "srqmd",
            "flow_ids": ["rtpflow_1", "rtpflow_2"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": True,
        }]

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


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

        self.__forward_bandwidths = None
        self.__forward_path_ctrler = None
        self.__forward_flow = None

        self.__backward_bandwidths = None
        self.__backward_path_ctrler = None
        self.__backward_flow = None

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_flow = RTPFlow(name="rtpflow_1",
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
            mprtp_ext_header_id = self.__mprtp_ext_header_id
            )

        self.__backward_flow = RTPFlow(name="rtpflow_2",
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
            flipped = True
            )

        result = [self.__forward_flow, self.__backward_flow]
        return result

    def __generate_path_ctrlers(self):
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

        self.__forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(forward_stages))
        self.__forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage = path_stage)

        self.__backward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(backward_stages))
        self.__backward_path_ctrler = PathShellCtrler(path_name="veth3", path_stage = path_stage)
        result = [self.__forward_path_ctrler, self.__backward_path_ctrler]

        return result

    def __generate_description(self):
        forward_flow = {
            "flow_id": "rtpflow_1",
            "title": "RTP 1",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow,
            "evaluations": None,
            "sources": None,
        }

        backward_flow = {
            "flow_id": "rtpflow_2",
            "title": "RTP 2",
            "path_ctrler": self.__backward_path_ctrler,
            "flow": self.__backward_flow,
            "evaluations": None,
            "sources": None,
        }
        return [forward_flow, backward_flow]

    def get_plot_description(self):
        return [{
            "plot_id": "forward_flows",
            "filename": '_'.join([self.name, "forward", str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "type": "srqmd",
            "flow_ids": ["rtpflow_1"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": True,
        },
        {
            "plot_id": "backward_flows",
            "filename": '_'.join([self.name, "backward", str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "type": "srqmd",
            "flow_ids": ["rtpflow_2"],
            "bandwidths": self.__backward_bandwidths,
            "plot_bandwidth": True,
        },
        ]

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


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

        self.__forward_bandwidths = None
        self.__forward_path_ctrler = None
        self.__forward_flow_1 = None
        self.__forward_flow_3 = None
        self.__forward_flow_3 = None

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_flow_1 = RTPFlow(name="rtpflow_1",
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
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            )

        self.__forward_flow_2 = RTPFlow(name="rtpflow_2",
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
            mprtp_ext_header_id = self.__mprtp_ext_header_id
            )

        self.__forward_flow_3 = RTPFlow(name="rtpflow_3",
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
            mprtp_ext_header_id = self.__mprtp_ext_header_id
            )

        result = [self.__forward_flow_1, self.__forward_flow_2, self.__forward_flow_3]
        return result

    def __generate_path_ctrlers(self):
        stages = [
        {
            "duration": 120,
            "config" : PathConfig(bandwidth = 3000, latency = self.__latency, jitter = self.__jitter)
        },
        ]
        self.__forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage = path_stage)

        result = [self.__forward_path_ctrler]
        return result

    def __generate_description(self):
        forward_flow_1 = {
            "flow_id": "rtpflow_1",
            "title": "Flow 1",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_1,
            "plot": None,
            "evaluations": None,
            "sources": None,
        }

        forward_flow_2 = {
            "flow_id": "rtpflow_2",
            "title": "Flow 2",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_2,
            "plot": None,
            "evaluations": None,
            "sources": None,
        }

        forward_flow_3 = {
            "flow_id": "rtpflow_3",
            "title": "Flow 3",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_3,
            "plot": None,
            "evaluations": None,
            "sources": None,
        }
        return [forward_flow_1, forward_flow_2, forward_flow_3]

    def get_plot_description(self):
        return [{
            "type": "srqmd",
            "plot_id": "flows",
            "filename": '_'.join([self.name, str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "flow_ids": ["rtpflow_1", "rtpflow_2", "rtpflow_3"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": True,
        }]

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
      return self.__description

class RMCAT5(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 0, fec_payload_type_id = 0):
        MyTest.__init__(self, "rmcat5", 300, algorithm, str(latency), str(jitter))

        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

        self.__forward_bandwidths = None
        self.__forward_path_ctrler = None
        self.__forward_flow_1 = None
        self.__forward_flow_3 = None
        self.__forward_flow_3 = None

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_flow_1 = RTPFlow(name="rtpflow_1",
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
            mprtp_ext_header_id = self.__mprtp_ext_header_id
            )

        self.__forward_flow_2 = RTPFlow(name="rtpflow_2",
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
            mprtp_ext_header_id = self.__mprtp_ext_header_id
            )

        self.__forward_flow_3 = RTPFlow(name="rtpflow_3",
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
            mprtp_ext_header_id = self.__mprtp_ext_header_id
            )

        result = [self.__forward_flow_1, self.__forward_flow_2, self.__forward_flow_3]
        return result

    def __generate_path_ctrlers(self):
        stages = [
        {
            "duration": 300,
            "config" : PathConfig(bandwidth = 3000, latency = self.__latency, jitter = self.__jitter)
        },
        ]
        self.__forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage = path_stage)

        result = [self.__forward_path_ctrler]
        return result

    def __generate_description(self):
        forward_flow_1 = {
            "flow_id": "rtpflow_1",
            "title": "Flow 1",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_1,
            "bandwidths": self.__forward_bandwidths,
            "plot": None,
            "evaluations": None,
            "sources": None,
        }

        forward_flow_2 = {
            "flow_id": "rtpflow_2",
            "title": "Flow 2",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_2,
            "plot": None,
            "evaluations": None,
            "sources": None,
        }

        forward_flow_3 = {
            "flow_id": "rtpflow_3",
            "title": "Flow 3",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_3,
            "plot": None,
            "evaluations": None,
            "sources": None,
        }
        return [forward_flow_1, forward_flow_2, forward_flow_3]

    def get_plot_description(self):
        return [{
            "type": "srqmd",
            "plot_id": "flows",
            "sr_title": "Sending Rates",
            "qmd_title": "Queue delay",
            "filename": '_'.join([self.name, str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "flow_ids": ["rtpflow_1", "rtpflow_2", "rtpflow_3"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": True,
        }]

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


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

        self.__forward_bandwidths = None
        self.__forward_path_ctrler = None
        self.__forward_pcap_ctrler = None
        self.__forward_rtp_flow = None
        self.__forward_tcp_flow = None
        self.__tcp_packetlog = "tcp_packets_1.pcap"

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_rtp_flow = RTPFlow(name="rtpflow_1",
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
            mprtp_ext_header_id = self.__mprtp_ext_header_id,

        )

        self.__forward_tcp_flow = TCPFlow(name="TCPLong flow", server_ip="10.0.0.6",
                                          server_port="12345", duration=120, packetlogs=[self.__tcp_packetlog]
                                          )
        return [self.__forward_rtp_flow, self.__forward_tcp_flow]

    def __generate_path_ctrlers(self):
        stages = [
        {
            "duration": 120,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        },
        ]
        self.__forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage = path_stage)
        self.__forward_pcap_listener = PathPcapListener(network_type = "tcp", network_interface = "veth2",
                                                        log_path = self.__tcp_packetlog)
        result = [self.__forward_path_ctrler, self.__forward_pcap_listener]
        return result

    def __generate_description(self):
        forward_rtp_flow = {
            "flow_id": "rtpflow",
            "title": "RTP",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_rtp_flow,
            "evaluations": None,
            "sources": None,
        }

        forward_tcp_flow = {
            "flow_id": "tcpflow",
            "title": "TCP",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_tcp_flow,
            "evaluations": None,
            "sources": None,
        }
        return [forward_rtp_flow, forward_tcp_flow]

    def get_plot_description(self):
        return [{
            "plot_id": "flows",
            "sr_title": "Sending Rates",
            "qmd_title": "Queue delay",
            "filename": '_'.join([self.name, str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "type": "srqmd",
            "flow_ids": ["rtpflow", "tcpflow"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": False,
        }]

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description

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

        self.__forward_bandwidths = None
        self.__forward_path_ctrler = None
        self.__forward_pcap_ctrler = None
        self.__forward_rtp_flow = None
        self.__forward_tcp_flow = None
        self.__tcp_packetlog = "tcp_packets.pcap"

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_rtp_flow = RTPFlow(name="rtpflow_1",
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
            mprtp_ext_header_id = self.__mprtp_ext_header_id
        )

        self.__forward_tcp_flow = TCPFlow(name = "TCPLong flow", server_ip = "10.0.0.6",
                                          server_port = "12345", duration = 300, create_client = False,
                                          packetlogs=[self.__tcp_packetlog])

        result = [self.__forward_rtp_flow, self.__forward_tcp_flow]
        start_time = 0
        while(start_time < 150):
            end = int(np.random.uniform(low = 1.0, high = 5.0))
            print (" end: " + str(end))
            start_time_plus = 0
            for x in range(0,end):
                duration = int(np.random.exponential(scale = 20.0)) + 1
                tcp_server = self.__forward_tcp_flow.tcp_server
                tcp_flow = TCPFlow(name = "TCPLong flow", server_ip = None, server_port = None,
                                   tcp_server = tcp_server, start_delay = start_time,
                                    duration = duration)
                start_time_plus = duration if start_time_plus < duration else start_time_plus
                start_time += int(np.random.uniform(low = 0, high = duration / 2)) + 1
                result.append(tcp_flow)
        return result

    def __generate_path_ctrlers(self):
        stages = [
        {
            "duration": 300,
            "config" : PathConfig(bandwidth=2000, latency=self.__latency, jitter=self.__jitter)
        },
        ]
        self.__forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage = path_stage)
        self.__forward_pcap_listener = PathPcapListener(network_type="tcp",
                                                        network_interface="veth2", log_path=self.__tcp_packetlog)
        result = [self.__forward_path_ctrler, self.__forward_pcap_listener]
        return result

    def __generate_description(self):
        forward_rtp_flow = {
            "flow_id": "rtpflow",
            "title": "RTP",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_rtp_flow,
            "evaluations": None,
            "sources": None,
        }

        forward_tcp_flow = {
            "flow_id": "tcpflow",
            "title": "TCP",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_tcp_flow,
            "evaluations": None,
            "sources": None,
        }
        return [forward_rtp_flow, forward_tcp_flow]

    def get_plot_description(self):
        return [{
            "plot_id": "flows",
            "sr_title": "Sending Rates",
            "qmd_title": "Queue delay",
            "filename": '_'.join([self.name, str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "type": "srqmd",
            "flow_ids": ["rtpflow", "tcpflow"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": False,
        }]

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description



class MPRTP1(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 3, fec_payload_type_id = 126):
        MyTest.__init__(self, "mprtp1", 125, algorithm, str(latency), str(jitter))

        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_1",
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=["10.0.0.6", "10.0.1.6"],
            rtp_ports=[5000, 5002],
            rtcp_ips=["10.0.0.1", "10.0.1.1"],
            rtcp_ports=[5001, 5003],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=[1,2]
            )

        result = [self.__forward_rtp_flow]
        return result

    def __generate_path_ctrlers(self):
        flow_stages_1 = [
        {
            "duration": 125,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        }]

        flow_stages_2 = [
        {
            "duration": 125,
            "config" : PathConfig(bandwidth=2000, latency=self.__latency, jitter=self.__jitter)
        }]

        self.__forward_bandwidths_1, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stages_1))
        self.__forward_path_ctrler_1 = PathShellCtrler(path_name="veth2", path_stage = path_stage)

        self.__forward_bandwidths_2, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stages_2))
        self.__forward_path_ctrler_2 = PathShellCtrler(path_name="veth6", path_stage = path_stage)
        result = [self.__forward_path_ctrler_1, self.__forward_path_ctrler_2]
        return result

    def __generate_description(self):
        forward_flow_1 = {
            "flow_id": "rtpsubflow_1",
            "title": "RTP",
            "fec_title": "RTP + FEC",
            "plot_fec": True,
            "path_ctrler": self.__forward_rtp_flow,
            "flow": self.__forward_rtp_flow,
            "bandwidths": self.__forward_bandwidths_1,
            "subflow_id": 1,
            "evaluations": None,
            "sources": None,
        }

        forward_flow_2 = {
            "flow_id": "rtpsubflow_2",
            "title": "RTP",
            "fec_title": "RTP + FEC",
            "plot_fec": True,
            "path_ctrler": self.__forward_rtp_flow,
            "flow": self.__forward_rtp_flow,
            "bandwidths": self.__forward_bandwidths_2,
            "subflow_id": 2,
            "evaluations": None,
            "sources": None,
        }
        return [forward_flow_1, forward_flow_2]

    def get_plot_description(self):
        return [{
            "type": "srqmd",
            "plot_id": "subflow_1",
            "sr_title": "Sending Rate for Subflow 1",
            "qmd_title": "Queue delay for Subflow 1",
            "filename": '_'.join([self.name, "srqmd", "subflow_1", str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "flow_ids": ["rtpsubflow_1"],
            "bandwidths": self.__forward_bandwidths_1,
            "plot_bandwidth": False,
        },
        {
            "type": "srqmd",
            "plot_id": "subflow_2",
            "sr_title": "Sending Rate for Subflow 2",
            "qmd_title": "Queue delay for Subflow 2",
            "filename": '_'.join(
                [self.name, "srqmd", "subflow_2", str(self.algorithm.name), str(self.latency) + "ms",
                 str(self.jitter) + "ms"]),
            "flow_ids": ["rtpsubflow_2"],
            "bandwidths": self.__forward_bandwidths_2,
            "plot_bandwidth": False,
        },
        {
            "type": "aggr",
            "sr_title": "Sending Rate for Subflow 1", # sending rates
            "disr_title": "Queue delay for Subflow 1", # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "flow_ids": ["rtpsubflow_2", "rtpflow_2"],
        }
        ]

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


class MPRTP2(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 3, fec_payload_type_id = 126):
        MyTest.__init__(self, "mprtp1", 125, algorithm, str(latency), str(jitter))

        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_1",
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=["10.0.0.6", "10.0.1.6"],
            rtp_ports=[5000, 5002],
            rtcp_ips=["10.0.0.1", "10.0.1.1"],
            rtcp_ports=[5001, 5003],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=[1,2]
            )

        result = [self.__forward_rtp_flow]
        return result

    def __generate_path_ctrlers(self):
        flow_stages_1 = [
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

        self.__forward_bandwidths_1, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stages_1))
        self.__forward_path_ctrler_1 = PathShellCtrler(path_name="veth2", path_stage = path_stage)

        flow_stages_2 = [
        {
            "duration": 125,
            "config" : PathConfig(bandwidth=2000, latency=self.__latency, jitter=self.__jitter)
        }
        ]

        self.__forward_bandwidths_1, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stages_1))
        self.__forward_path_ctrler_1 = PathShellCtrler(path_name="veth2", path_stage = path_stage)

        self.__forward_bandwidths_2, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stages_2))
        self.__forward_path_ctrler_2 = PathShellCtrler(path_name="veth6", path_stage = path_stage)
        result = [self.__forward_path_ctrler_1, self.__forward_path_ctrler_2]
        return result

    def __generate_description(self):
        forward_flow_1 = {
            "flow_id": "rtpsubflow_1",
            "title": "RTP",
            "fec_title": "RTP + FEC",
            "plot_fec": True,
            "path_ctrler": self.__forward_rtp_flow,
            "flow": self.__forward_rtp_flow,
            "bandwidths": self.__forward_bandwidths_1,
            "subflow_id": 1,
            "evaluations": None,
            "sources": None,
        }

        forward_flow_2 = {
            "flow_id": "rtpsubflow_2",
            "title": "RTP",
            "fec_title": "RTP + FEC",
            "plot_fec": True,
            "path_ctrler": self.__forward_rtp_flow,
            "flow": self.__forward_rtp_flow,
            "bandwidths": self.__forward_bandwidths_2,
            "subflow_id": 2,
            "evaluations": None,
            "sources": None,
        }
        return [forward_flow_1, forward_flow_2]

    def get_plot_description(self):
        return [{
            "type": "srqmd",
            "plot_id": "subflow_1",
            "sr_title": "Sending Rate for Subflow 1",
            "qmd_title": "Queue delay for Subflow 1",
            "filename": '_'.join([self.name, "srqmd", "subflow_1", str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "flow_ids": ["rtpsubflow_1"],
            "bandwidths": self.__forward_bandwidths_1,
            "plot_bandwidth": True,
        },
        {
            "type": "srqmd",
            "plot_id": "subflow_2",
            "sr_title": "Sending Rate for Subflow 2",
            "qmd_title": "Queue delay for Subflow 2",
            "filename": '_'.join(
                [self.name, "srqmd", "subflow_2", str(self.algorithm.name), str(self.latency) + "ms",
                 str(self.jitter) + "ms"]),
            "flow_ids": ["rtpsubflow_2"],
            "bandwidths": self.__forward_bandwidths_2,
            "plot_bandwidth": False,
        },
        {
            "type": "aggr",
            "sr_title": "Sending Rate for Subflow 1", # sending rates
            "disr_title": "Queue delay for Subflow 1", # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "flow_ids": ["rtpsubflow_2", "rtpflow_2"],
        }
        ]

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description
