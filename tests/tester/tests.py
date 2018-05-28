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
        # {
        #     "duration": 125,
        #     "config": PathConfig(bandwidth=3000, latency=self.__latency, jitter=self.__jitter)
        # },
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
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 3, fec_payload_type_id = 126, subflows_num=2):
        MyTest.__init__(self, "mprtp1", 150, algorithm, str(latency), str(jitter))

        self._forward_bandwidths = []
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id
        self._subflows_num = subflows_num

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self._flow_ids = []
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                    "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5000, 5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018]
        rtcp_ports = [5001, 5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_1" + "_" + str(self._subflows_num),
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=rtp_ips[:self._subflows_num],
            rtp_ports=rtp_ports[:self._subflows_num],
            rtcp_ips=rtcp_ips[:self._subflows_num],
            rtcp_ports=rtcp_ports[:self._subflows_num],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=subflow_ids[:self._subflows_num]
            )

        result = [self.__forward_rtp_flow]
        return result

    def __generate_path_ctrlers(self):
        result = []
        path_names = ["veth2", "veth6", "veth10", "veth14", "veth18",
                      "veth22", "veth26", "veth30", "veth34", "veth38"]
        max_bw = max(800, int(5000 / self._subflows_num))
        for subflow_id in range(self._subflows_num):
            flow_stage = [
            {
                "duration": 150,
                "config": PathConfig(bandwidth=max_bw, latency=self.__latency, jitter=self.__jitter)
            }
            ]
            forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stage))
            forward_path_ctrler = PathShellCtrler(path_name=path_names[subflow_id], path_stage=path_stage)
            self._forward_bandwidths.append(forward_bandwidths)
            result.append(forward_path_ctrler)
        return result

    def __generate_description(self):
        self._flow_ids = ["rtpsubflow_1", "rtpsubflow_2", "rtpsubflow_3", "rtpsubflow_4", "rtpsubflow_5",
                          "rtpsubflow_6", "rtpsubflow_7", "rtpsubflow_8", "rtpsubflow_9", "rtpsubflow_10"]
        result = []
        for subflow_id in range(self._subflows_num):
            forward_flow = {
                "flow_id": self._flow_ids[subflow_id],
                "title": "RTP",
                "fec_title": "RTP + FEC",
                "plot_fec": True,
                "path_ctrler": self.__forward_rtp_flow,
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[subflow_id],
                "subflow_id": subflow_id + 1,
                "evaluations": None,
                "sources": None,
            }
            result.append(forward_flow)
        return result

    def get_plot_description(self):
        result = []
        for subflow_id in range(self._subflows_num):
            result.append({
                "type": "srqmd",
                "plot_id": "subflow_" + str(subflow_id + 1),
                "sr_title": "Sending Rate for Subflow " + str(subflow_id + 1),
                "qmd_title": "Queue delay for Subflow " + str(subflow_id + 1),
                "filename": '_'.join(
                    [self.name, "srqmd", "subflow_" + str(subflow_id), str(self.algorithm.name), str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": self._flow_ids[subflow_id],
                "bandwidths": self._forward_bandwidths[subflow_id],
                "plot_bandwidth": True,
            })
        bandwidths = [self._forward_bandwidths[0][i] * self._subflows_num for i in range(len(self._forward_bandwidths[0]))]
        # print(bandwidths)
        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description



class MPRTP2(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 3, fec_payload_type_id = 126, subflows_num=2):
        MyTest.__init__(self, "mprtp2", 150, algorithm, str(latency), str(jitter))

        self._forward_bandwidths = []
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id
        self._subflows_num = subflows_num

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self._flow_ids = []
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                    "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5000, 5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018]
        rtcp_ports = [5001, 5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_2" + "_" + str(self._subflows_num),
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=rtp_ips[:self._subflows_num],
            rtp_ports=rtp_ports[:self._subflows_num],
            rtcp_ips=rtcp_ips[:self._subflows_num],
            rtcp_ports=rtcp_ports[:self._subflows_num],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=subflow_ids[:self._subflows_num]
            )

        result = [self.__forward_rtp_flow]
        return result

    def __generate_path_ctrlers(self):
        result = []
        path_names = ["veth2", "veth6", "veth10", "veth14", "veth18",
                      "veth22", "veth26", "veth30", "veth34", "veth38"]
        # max_bw = max(800, int(5000 / self._subflows_num))
        # min_bw = max(400, int(2500 / self._subflows_num))
        max_bw = 1000
        min_bw = 500
        for subflow_id in range(self._subflows_num):
            flow_stage = [
            {
                "duration": 25,
                "config": PathConfig(bandwidth=min_bw, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=max_bw, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=min_bw, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=max_bw, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 50,
                "config": PathConfig(bandwidth=min_bw, latency=self.__latency, jitter=self.__jitter)
            }]
            forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stage))
            forward_path_ctrler = PathShellCtrler(path_name=path_names[subflow_id], path_stage=path_stage)
            self._forward_bandwidths.append(forward_bandwidths)
            result.append(forward_path_ctrler)
        return result

    def __generate_description(self):
        self._flow_ids = ["rtpsubflow_1", "rtpsubflow_2", "rtpsubflow_3", "rtpsubflow_4", "rtpsubflow_5",
                          "rtpsubflow_6", "rtpsubflow_7", "rtpsubflow_8", "rtpsubflow_9", "rtpsubflow_10"]
        result = []
        for subflow_id in range(self._subflows_num):
            forward_flow = {
                "flow_id": self._flow_ids[subflow_id],
                "title": "RTP",
                "fec_title": "RTP + FEC",
                "plot_fec": True,
                "path_ctrler": self.__forward_rtp_flow,
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[subflow_id],
                "subflow_id": subflow_id + 1,
                "evaluations": None,
                "sources": None,
            }
            result.append(forward_flow)
        return result

    def get_plot_description(self):
        result = []
        for subflow_id in range(self._subflows_num):
            result.append({
                "type": "srqmd",
                "plot_id": "subflow_" + str(subflow_id + 1),
                "sr_title": "Sending Rate for Subflow " + str(subflow_id + 1),
                "qmd_title": "Queue delay for Subflow " + str(subflow_id + 1),
                "filename": '_'.join(
                    [self.name, "srqmd", "subflow_" + str(subflow_id), str(self.algorithm.name), str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": self._flow_ids[subflow_id],
                "bandwidths": self._forward_bandwidths[subflow_id],
                "plot_bandwidth": True,
            })
        bandwidths = [self._forward_bandwidths[0][i] * self._subflows_num for i in range(len(self._forward_bandwidths[0]))]
        # print(bandwidths)
        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


class MPRTP3(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 3, fec_payload_type_id = 126, subflows_num=2):
        MyTest.__init__(self, "mprtp3", max(150, 25 * subflows_num + 50), algorithm, str(latency), str(jitter))

        self._forward_bandwidths = []
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id
        self._subflows_num = subflows_num

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self._flow_ids = []
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                   "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5000, 5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018]
        rtcp_ports = [5001, 5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_3" + "_" + str(self._subflows_num),
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=rtp_ips[:self._subflows_num],
            rtp_ports=rtp_ports[:self._subflows_num],
            rtcp_ips=rtcp_ips[:self._subflows_num],
            rtcp_ports=rtcp_ports[:self._subflows_num],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=subflow_ids[:self._subflows_num]
            )

        result = [self.__forward_rtp_flow]
        return result

    def __generate_path_ctrlers(self):
        result = []
        path_names=["veth2", "veth6", "veth10", "veth14", "veth18", "veth22", "veth26", "veth30", "veth34", "veth38"]
        offset = 1000
        min_bw = max(500, int(2000 / self._subflows_num))
        duration = max(150, 25 * self._subflows_num + 50)
        for subflow_id in range(self._subflows_num):
            flow_stage = [
            {
                "duration": 25 * subflow_id,
                "config": PathConfig(bandwidth=min_bw, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=min_bw + offset, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25 * (self._subflows_num - subflow_id - 1),
                "config": PathConfig(bandwidth=min_bw, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 50,
                "config": PathConfig(bandwidth=min_bw + int(offset / self._subflows_num), latency=self.__latency, jitter=self.__jitter)
            }
            ]
            forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stage))
            forward_path_ctrler = PathShellCtrler(path_name=path_names[subflow_id], path_stage=path_stage)
            self._forward_bandwidths.append(forward_bandwidths)
            result.append(forward_path_ctrler)
        return result

    def __generate_description(self):
        self._flow_ids = ["rtpsubflow_1", "rtpsubflow_2", "rtpsubflow_3", "rtpsubflow_4", "rtpsubflow_5",
                          "rtpsubflow_6", "rtpsubflow_7", "rtpsubflow_8", "rtpsubflow_9", "rtpsubflow_10"]
        result = []
        for subflow_id in range(self._subflows_num):
            forward_flow = {
                "flow_id": self._flow_ids[subflow_id],
                "title": "RTP",
                "fec_title": "RTP + FEC",
                "plot_fec": True,
                "path_ctrler": self.__forward_rtp_flow,
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[subflow_id],
                "subflow_id": subflow_id + 1,
                "evaluations": None,
                "sources": None,
            }
            result.append(forward_flow)
        return result

    def get_plot_description(self):
        result = []
        for subflow_id in range(self._subflows_num):
            result.append({
                "type": "srqmd",
                "plot_id": "subflow_" + str(subflow_id + 1),
                "sr_title": "Sending Rate for Subflow " + str(subflow_id + 1),
                "qmd_title": "Queue delay for Subflow " + str(subflow_id + 1),
                "filename": '_'.join(
                    [self.name, "srqmd", "subflow_" + str(subflow_id), str(self.algorithm.name), str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": self._flow_ids[subflow_id],
                "bandwidths": self._forward_bandwidths[subflow_id],
                "plot_bandwidth": True,
            })
        bandwidths = [0] * len(self._forward_bandwidths[0])
        for subflow_id in range(self._subflows_num):
            for i in range(len(self._forward_bandwidths[0])):
                bandwidths[i] += self._forward_bandwidths[subflow_id][i]

        # print(bandwidths)
        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


class MPRTP4(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 3, fec_payload_type_id = 126, subflows_num=2):
        MyTest.__init__(self, "mprtp4", 100, algorithm, str(latency), str(jitter))

        self._forward_bandwidths = []
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id
        self._subflows_num = subflows_num

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self._flow_ids = []
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                   "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5000, 5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018]
        rtcp_ports = [5001, 5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_4" + "_" + str(self._subflows_num),
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=rtp_ips[:self._subflows_num],
            rtp_ports=rtp_ports[:self._subflows_num],
            rtcp_ips=rtcp_ips[:self._subflows_num],
            rtcp_ports=rtcp_ports[:self._subflows_num],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=subflow_ids[:self._subflows_num]
            )

        result = [self.__forward_rtp_flow]
        return result

    def __generate_path_ctrlers(self):
        result = []
        path_names=["veth2", "veth6", "veth10", "veth14", "veth18", "veth22", "veth26", "veth30", "veth34", "veth38"]
        offset = 1500
        # min_bw = max(400, int(2000 / self._subflows_num))
        min_bw = 1000
        space = max(1, self._subflows_num - 1)
        flow_stages_1 = [
            {
                "duration": 25,
                "config": PathConfig(bandwidth=1000, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=500, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=1000, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=500, latency=self.__latency, jitter=self.__jitter)
            },
        ]
        flow_stage = deque(flow_stages_1)
        forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(flow_stage)
        forward_path_ctrler = PathShellCtrler(path_name=path_names[0], path_stage=path_stage)
        self._forward_bandwidths.append(forward_bandwidths)
        result.append(forward_path_ctrler)

        flow_stages_2 = [
            {
                "duration": 25,
                "config": PathConfig(bandwidth=500, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=1000, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=500, latency=self.__latency, jitter=self.__jitter)
            },
            {
                "duration": 25,
                "config": PathConfig(bandwidth=1000, latency=self.__latency, jitter=self.__jitter)
            },
        ]
        flow_stage = deque(flow_stages_2)
        forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(flow_stage)
        forward_path_ctrler = PathShellCtrler(path_name=path_names[1], path_stage=path_stage)
        self._forward_bandwidths.append(forward_bandwidths)

        result.append(forward_path_ctrler)
        return result

    def __generate_description(self):
        self._flow_ids = ["rtpsubflow_1", "rtpsubflow_2", "rtpsubflow_3", "rtpsubflow_4", "rtpsubflow_5",
                          "rtpsubflow_6", "rtpsubflow_7", "rtpsubflow_8", "rtpsubflow_9", "rtpsubflow_10"]
        result = []
        for subflow_id in range(self._subflows_num):
            forward_flow = {
                "flow_id": self._flow_ids[subflow_id],
                "title": "RTP",
                "fec_title": "RTP + FEC",
                "plot_fec": True,
                "path_ctrler": self.__forward_rtp_flow,
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[subflow_id],
                "subflow_id": subflow_id + 1,
                "evaluations": None,
                "sources": None,
            }
            result.append(forward_flow)
        return result

    def get_plot_description(self):
        result = []
        for subflow_id in range(self._subflows_num):
            result.append({
                "type": "srqmd",
                "plot_id": "subflow_" + str(subflow_id + 1),
                "sr_title": "Sending Rate for Subflow " + str(subflow_id + 1),
                "qmd_title": "Queue delay for Subflow " + str(subflow_id + 1),
                "filename": '_'.join(
                    [self.name, "srqmd", "subflow_" + str(subflow_id), str(self.algorithm.name), str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": self._flow_ids[subflow_id],
                "bandwidths": self._forward_bandwidths[subflow_id],
                "plot_bandwidth": True,
            })
        bandwidths = [0] * len(self._forward_bandwidths[0])
        for subflow_id in range(self._subflows_num):
            for i in range(len(self._forward_bandwidths[0])):
                bandwidths[i] += self._forward_bandwidths[subflow_id][i]

        # print(bandwidths)
        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


class MPRTP5(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header=3,
                 fec_payload_type_id=126, subflows_num=2, tcp=False):
        MyTest.__init__(self, "mprtp5", 120, algorithm, str(latency), str(jitter))
        self._tcp = tcp
        self._subflows_num = subflows_num
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

        self.__tcp_packetlog = "tcp_packets_1.pcap"

        self.__forward_bandwidths = None
        self.__forward_path_ctrler = None
        self.__forward_pcap_listener = None
        self.__forward_flow_1 = None
        self.__forward_flow_2 = None
        self.__forward_flow_3 = None

        self._flow_ids = ["rtpsubflow_1", "rtpsubflow_2", "rtpsubflow_3", "rtpsubflow_4", "rtpsubflow_5",
                          "rtpsubflow_6", "rtpsubflow_7", "rtpsubflow_8", "rtpsubflow_9", "rtpsubflow_10"]

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_flow_1 = None
        if self._tcp is True:
            self.__forward_flow_1 = \
                TCPFlow(name="singleflow",
                        server_ip="10.0.0.6",
                        server_port="12345",
                        duration=120,
                        packetlogs=[self.__tcp_packetlog]
                )
        else:
            self.__forward_flow_1 =\
                RTPFlow(name="singleflow",
                    path="./",
                    flownum=1,
                    codec=Codecs.VP8,
                    algorithm=self.algorithm,
                    rtp_ip="10.0.0.6",
                    rtp_port=5000,
                    rtcp_ip="10.0.0.1",
                    rtcp_port=5001,
                    start_delay=0,
                    source_type=self.__source_type,
                    sink_type=self.__sink_type,
                    mprtp_ext_header_id=self.__mprtp_ext_header_id,
                    )
        rtp_ips = ["10.0.0.6", "10.0.0.6", "10.0.0.6", "10.0.0.6", "10.0.0.6", "10.0.0.6", "10.0.0.6", "10.0.0.6",
                   "10.0.0.6", "10.0.0.6"]
        rtcp_ips = ["10.0.0.1", "10.0.0.1", "10.0.0.1", "10.0.0.1", "10.0.0.1", "10.0.0.1", "10.0.0.1", "10.0.0.1",
                    "10.0.0.1", "10.0.0.1"]
        rtp_ports = [5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018, 5020]
        rtcp_ports = [5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019, 5021]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

        self.__forward_flow_2 = MPRTPFlow(name="mpflow" + "_" + str(self._subflows_num),
            path="./",
            flownum=2,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=rtp_ips[:self._subflows_num],
            rtp_ports=rtp_ports[:self._subflows_num],
            rtcp_ips=rtcp_ips[:self._subflows_num],
            rtcp_ports=rtcp_ports[:self._subflows_num],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=subflow_ids[:self._subflows_num]
            )

        result = [self.__forward_flow_1, self.__forward_flow_2]
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
        if self._tcp is True:
            self.__forward_pcap_listener = PathPcapListener(network_type="tcp", network_interface="veth2",
                                                            log_path=self.__tcp_packetlog)
            result.append(self.__forward_pcap_listener)

        return result

    def __generate_description(self):
        forward_flow_1 = {
            "flow_id": "flow_1",
            "title": "RTP Flow",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_1,
            "plot": None,
            "evaluations": None,
            "sources": None,
        } if self._tcp is False else {
            "flow_id": "flow_1",
            "title": "TCP Flow",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_1,
            "evaluations": None,
            "sources": None,
        }

        result = [forward_flow_1]

        for subflow_id in range(self._subflows_num):
            forward_flow = {
                "flow_id": self._flow_ids[subflow_id],
                "title": "MPRTP Subflow " + str(subflow_id+1),
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__forward_path_ctrler,
                "flow": self.__forward_flow_2,
                "bandwidths": None,
                "subflow_id": subflow_id + 1,
                "evaluations": None,
                "sources": None,
            }
            result.append(forward_flow)

        return result

    def get_plot_description(self):
        result = [{
            "type": "srqmd",
            "plot_id": "flows",
            "filename": '_'.join([self.name, str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num] + ["flow_1"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": False,
        }]
        # bandwidths = [0] * len(self._forward_bandwidths[0])
        # for subflow_id in range(self._subflows_num):
        #     for i in range(len(self._forward_bandwidths[0])):
        #         bandwidths[i] += self._forward_bandwidths[subflow_id][i]

        for subflow_id in range(self._subflows_num):
            result.append({
                "type": "srqmd",
                "plot_id": "subflow_" + str(subflow_id + 1),
                "sr_title": "Sending Rate for Subflow " + str(subflow_id + 1),
                "qmd_title": "Queue delay for Subflow " + str(subflow_id + 1),
                "filename": '_'.join(
                    [self.name, "srqmd", "subflow_" + str(subflow_id), str(self.algorithm.name),
                     str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": [self._flow_ids[subflow_id]],
                "plot_bandwidth": False,
                "plot_fec": False,
                "bandwidths": self.__forward_bandwidths,
            })

        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num],
            "plot_bandwidth": False,
            "plot_fec": False,
            "bandwidths": self.__forward_bandwidths,
            "title": "MPRTP"
        })
        print(result)
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
      return self.__description



class MPRTP6(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header=3,
                 fec_payload_type_id=126, subflows_num=2, tcp=False):
        MyTest.__init__(self, "mprtp6", 120, algorithm, str(latency), str(jitter))
        self._tcp = tcp
        self._subflows_num = subflows_num
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

        self.__tcp_packetlog = "tcp_packets_1.pcap"

        self.__forward_bandwidths = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler = None
        self.__forward_path_ctrler_2 = None
        self.__forward_pcap_listener = None
        self.__forward_flow_1 = None
        self.__forward_flow_2 = None
        self.__forward_flow_3 = None

        self._flow_ids = ["rtpsubflow_1", "rtpsubflow_2", "rtpsubflow_3", "rtpsubflow_4", "rtpsubflow_5",
                          "rtpsubflow_6", "rtpsubflow_7", "rtpsubflow_8", "rtpsubflow_9", "rtpsubflow_10"]

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_flow_1 = None
        if self._tcp is True:
            self.__forward_flow_1 = \
                TCPFlow(name="singleflow",
                        server_ip="10.0.0.6",
                        server_port="12345",
                        duration=120,
                        packetlogs=[self.__tcp_packetlog]
                )
        else:
            self.__forward_flow_1 =\
                RTPFlow(name="singleflow",
                    path="./",
                    flownum=1,
                    codec=Codecs.VP8,
                    algorithm=self.algorithm,
                    rtp_ip="10.0.0.6",
                    rtp_port=5000,
                    rtcp_ip="10.0.0.1",
                    rtcp_port=5001,
                    start_delay=0,
                    source_type=self.__source_type,
                    sink_type=self.__sink_type,
                    mprtp_ext_header_id=self.__mprtp_ext_header_id,
                    )
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                   "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018, 5020]
        rtcp_ports = [5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019, 5021]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

        self.__forward_flow_2 = MPRTPFlow(name="mpflow" + "_" + str(self._subflows_num),
            path="./",
            flownum=2,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=rtp_ips[:self._subflows_num],
            rtp_ports=rtp_ports[:self._subflows_num],
            rtcp_ips=rtcp_ips[:self._subflows_num],
            rtcp_ports=rtcp_ports[:self._subflows_num],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=subflow_ids[:self._subflows_num]
            )

        result = [self.__forward_flow_1, self.__forward_flow_2]
        return result

    def __generate_path_ctrlers(self):
        stages = [
        {
            "duration": 120,
            "config" : PathConfig(bandwidth = 2000, latency = self.__latency, jitter = self.__jitter)
        },
        ]
        self.__forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage=path_stage)

        stages = [
            {
                "duration": 120,
                "config": PathConfig(bandwidth=2000, latency=self.__latency, jitter=self.__jitter)
            },
        ]
        self.__forward_bandwidths_2, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler_2 = PathShellCtrler(path_name="veth6", path_stage=path_stage)


        result = [self.__forward_path_ctrler, self.__forward_path_ctrler_2]
        if self._tcp is True:
            self.__forward_pcap_listener = PathPcapListener(network_type="tcp", network_interface="veth2",
                                                            log_path=self.__tcp_packetlog)
            result.append(self.__forward_pcap_listener)

        return result

    def __generate_description(self):
        forward_flow_1 = {
            "flow_id": "flow_1",
            "title": "RTP Flow",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_1,
            "plot": None,
            "evaluations": None,
            "sources": None,
        } if self._tcp is False else {
            "flow_id": "flow_1",
            "title": "TCP Flow",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_1,
            "evaluations": None,
            "sources": None,
        }

        result = [forward_flow_1]

        result.append({
                "flow_id": self._flow_ids[0],
                "title": "MPRTP Subflow 1",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__forward_path_ctrler,
                "flow": self.__forward_flow_2,
                "bandwidths": None,
                "subflow_id": 1,
                "evaluations": None,
                "sources": None,
            })

        result.append({
                "flow_id": self._flow_ids[1],
                "title": "MPRTP Subflow 2",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__forward_path_ctrler_2,
                "flow": self.__forward_flow_2,
                "bandwidths": None,
                "subflow_id": 2,
                "evaluations": None,
                "sources": None,
            })

        return result

    def get_plot_description(self):
        result = [{
            "type": "srqmd",
            "plot_id": "flows",
            "filename": '_'.join([self.name, str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "flow_ids": [self._flow_ids[0]] + ["flow_1"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": False,
        }]

        result.append({
            "type": "srqmd",
            "plot_id": "subflow_1",
            "sr_title": "Sending Rate for Subflow 1",
            "qmd_title": "Queue delay for Subflow 1",
            "filename": '_'.join(
                [self.name, "srqmd", "subflow_1", str(self.algorithm.name),
                 str(self.latency) + "ms",
                 str(self.jitter) + "ms"]),
            "flow_ids": [self._flow_ids[0]],
            "plot_bandwidth": False,
            "plot_fec": False,
            "bandwidths": self.__forward_bandwidths,
        })

        result.append({
            "type": "srqmd",
            "plot_id": "subflow_2",
            "sr_title": "Sending Rate for Subflow 2",
            "qmd_title": "Queue delay for Subflow 2",
            "filename": '_'.join(
                [self.name, "srqmd", "subflow_2", str(self.algorithm.name),
                 str(self.latency) + "ms",
                 str(self.jitter) + "ms"]),
            "flow_ids": [self._flow_ids[1]],
            "plot_bandwidth": False,
            "plot_fec": False,
            "bandwidths": self.__forward_bandwidths_2,
        })

        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num],
            "plot_bandwidth": False,
            "plot_fec": False,
            "bandwidths": [3000] * 1200,
            "title": "MPRTP"
        })
        print(result)
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
      return self.__description



class MPRTP7(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header=3,
                 fec_payload_type_id=126, subflows_num=2, tcp=False):
        MyTest.__init__(self, "mprtp7", 120, algorithm, str(latency), str(jitter))
        self._tcp = tcp
        self._subflows_num = subflows_num
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id

        self.__tcp_packetlog = "tcp_packets_1.pcap"
        self.__tcp_packetlog_2 = "tcp_packets_1.pcap"

        self.__forward_bandwidths = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler = None
        self.__forward_path_ctrler_2 = None
        self.__forward_path_ctrler_3 = None
        self.__forward_pcap_listener = None
        self.__forward_pcap_listener_2 = None
        self.__forward_flow_1 = None
        self.__forward_flow_2 = None
        self.__forward_flow_3 = None

        self._flow_ids = ["rtpsubflow_1", "rtpsubflow_2", "rtpsubflow_3", "rtpsubflow_4", "rtpsubflow_5",
                          "rtpsubflow_6", "rtpsubflow_7", "rtpsubflow_8", "rtpsubflow_9", "rtpsubflow_10"]

        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        self.__forward_flow_1 = None
        if self._tcp is True:
            self.__forward_flow_1 = \
                TCPFlow(name="singleflow_1",
                        server_ip="10.0.0.6",
                        server_port="12345",
                        duration=120,
                        packetlogs=[self.__tcp_packetlog]
                )
        else:
            self.__forward_flow_1 =\
                RTPFlow(name="singleflow_1",
                    path="./",
                    flownum=1,
                    codec=Codecs.VP8,
                    algorithm=self.algorithm,
                    rtp_ip="10.0.0.6",
                    rtp_port=5000,
                    rtcp_ip="10.0.0.1",
                    rtcp_port=5001,
                    start_delay=0,
                    source_type=self.__source_type,
                    sink_type=self.__sink_type,
                    mprtp_ext_header_id=self.__mprtp_ext_header_id,
                    )
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                   "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018, 5020]
        rtcp_ports = [5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019, 5021]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

        self.__forward_flow_2 = MPRTPFlow(name="mpflow" + "_" + str(self._subflows_num),
            path="./",
            flownum=2,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=rtp_ips[:self._subflows_num],
            rtp_ports=rtp_ports[:self._subflows_num],
            rtcp_ips=rtcp_ips[:self._subflows_num],
            rtcp_ports=rtcp_ports[:self._subflows_num],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=subflow_ids[:self._subflows_num]
            )

        self.__forward_flow_3 = \
            RTPFlow(name="singleflow_2",
                    path="./",
                    flownum=3,
                    codec=Codecs.VP8,
                    algorithm=self.algorithm,
                    rtp_ip="10.0.1.6",
                    rtp_port=5002,
                    rtcp_ip="10.0.1.1",
                    rtcp_port=5003,
                    start_delay=0,
                    source_type=self.__source_type,
                    sink_type=self.__sink_type,
                    mprtp_ext_header_id=self.__mprtp_ext_header_id,
                    )

        result = [self.__forward_flow_1, self.__forward_flow_2, self.__forward_flow_3]
        return result

    def __generate_path_ctrlers(self):
        stages = [
        {
            "duration": 120,
            "config" : PathConfig(bandwidth=2000, latency = self.__latency, jitter = self.__jitter)
        },
        ]
        self.__forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler = PathShellCtrler(path_name="veth2", path_stage=path_stage)

        stages = [
            {
                "duration": 120,
                "config": PathConfig(bandwidth=2000, latency=self.__latency, jitter=self.__jitter)
            },
        ]
        self.__forward_bandwidths_2, path_stage = self.make_bandwidths_and_path_stage(deque(stages))
        self.__forward_path_ctrler_2 = PathShellCtrler(path_name="veth6", path_stage=path_stage)


        result = [self.__forward_path_ctrler, self.__forward_path_ctrler_2]
        if self._tcp is True:
            self.__forward_pcap_listener = PathPcapListener(network_type="tcp", network_interface="veth2",
                                                            log_path=self.__tcp_packetlog)
            result.append(self.__forward_pcap_listener)
            # self.__forward_pcap_listener_2 = PathPcapListener(network_type="tcp", network_interface="veth6",
            #                                                 log_path=self.__tcp_packetlog)
            # result.append(self.__forward_pcap_listener_2)

        return result

    def __generate_description(self):
        forward_flow_1 = {
            "flow_id": "flow_1",
            "title": "RTP Flow",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_1,
            "plot": None,
            "evaluations": None,
            "sources": None,
        } if self._tcp is False else {
            "flow_id": "flow_1",
            "title": "TCP Flow",
            "path_ctrler": self.__forward_path_ctrler,
            "flow": self.__forward_flow_1,
            "evaluations": None,
            "sources": None,
        }

        result = [forward_flow_1]

        result.append({
                "flow_id": self._flow_ids[0],
                "title": "MPRTP Subflow 1",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__forward_path_ctrler,
                "flow": self.__forward_flow_2,
                "bandwidths": None,
                "subflow_id": 1,
                "evaluations": None,
                "sources": None,
            })

        forward_flow_3 = {
            "flow_id": "flow_2",
            "title": "RTP Flow",
            "path_ctrler": self.__forward_path_ctrler_2,
            "flow": self.__forward_flow_3,
            "plot": None,
            "evaluations": None,
            "sources": None,
        }

        result.append(forward_flow_3)

        result.append({
                "flow_id": self._flow_ids[1],
                "title": "MPRTP Subflow 2",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__forward_path_ctrler_2,
                "flow": self.__forward_flow_2,
                "bandwidths": None,
                "subflow_id": 2,
                "evaluations": None,
                "sources": None,
            })

        return result

    def get_plot_description(self):
        result = [{
            "type": "srqmd",
            "plot_id": "flows_1",
            "filename": '_'.join([self.name + "_1", str(self.algorithm.name), str(self.latency) + "ms",
                                 str(self.jitter) + "ms"]),
            "flow_ids": [self._flow_ids[0]] + ["flow_1"],
            "bandwidths": self.__forward_bandwidths,
            "plot_bandwidth": False,
        },
        {
            "type": "srqmd",
            "plot_id": "flows_2",
            "filename": '_'.join([self.name + "_2", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": [self._flow_ids[1]] + ["flow_2"],
            "bandwidths": self.__forward_bandwidths_2,
            "plot_bandwidth": False,
        }
        ]

        result.append({
            "type": "srqmd",
            "plot_id": "subflow_1",
            "sr_title": "Sending Rate for Subflow 1",
            "qmd_title": "Queue delay for Subflow 1",
            "filename": '_'.join(
                [self.name, "srqmd", "subflow_1", str(self.algorithm.name),
                 str(self.latency) + "ms",
                 str(self.jitter) + "ms"]),
            "flow_ids": [self._flow_ids[0]],
            "plot_bandwidth": False,
            "plot_fec": False,
            "bandwidths": self.__forward_bandwidths,
        })

        result.append({
            "type": "srqmd",
            "plot_id": "subflow_2",
            "sr_title": "Sending Rate for Subflow 2",
            "qmd_title": "Queue delay for Subflow 2",
            "filename": '_'.join(
                [self.name, "srqmd", "subflow_2", str(self.algorithm.name),
                 str(self.latency) + "ms",
                 str(self.jitter) + "ms"]),
            "flow_ids": [self._flow_ids[1]],
            "plot_bandwidth": False,
            "plot_fec": False,
            "bandwidths": self.__forward_bandwidths_2,
        })

        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num],
            "plot_bandwidth": False,
            "plot_fec": False,
            "bandwidths": [4000] * 1200,
            "title": "MPRTP"
        })
        print(result)
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
      return self.__description


class MPRTP8(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header = 3, fec_payload_type_id = 126, subflows_num=2):
        MyTest.__init__(self, "mprtp8", 120, algorithm, str(latency), str(jitter))

        self._forward_bandwidths = []
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id
        self._subflows_num = subflows_num

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self._flow_ids = []
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                   "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5000, 5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018]
        rtcp_ports = [5001, 5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_8" + "_" + str(self._subflows_num),
            path="./",
            flownum=1,
            codec=Codecs.VP8,
            algorithm=self.algorithm,
            rtp_ips=rtp_ips[:self._subflows_num],
            rtp_ports=rtp_ports[:self._subflows_num],
            rtcp_ips=rtcp_ips[:self._subflows_num],
            rtcp_ports=rtcp_ports[:self._subflows_num],
            start_delay = 0,
            source_type = self.__source_type,
            sink_type = self.__sink_type,
            mprtp_ext_header_id = self.__mprtp_ext_header_id,
            subflow_ids=subflow_ids[:self._subflows_num]
            )

        result = [self.__forward_rtp_flow]
        return result

    def __generate_path_ctrlers(self):
        result = []
        path_names=["veth2", "veth6", "veth10", "veth14", "veth18", "veth22", "veth26", "veth30", "veth34", "veth38"]
        for subflow_id in range(self._subflows_num):
            flow_stage = [
            {
                "duration": 120,
                "config": PathConfig(bandwidth=1000, latency=self.__latency, jitter=self.__jitter)
            }
            ]
            forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stage))
            forward_path_ctrler = PathShellCtrler(path_name=path_names[subflow_id], path_stage=path_stage)
            self._forward_bandwidths.append(forward_bandwidths)
            result.append(forward_path_ctrler)
        return result

    def __generate_description(self):
        self._flow_ids = ["rtpsubflow_1", "rtpsubflow_2", "rtpsubflow_3", "rtpsubflow_4", "rtpsubflow_5",
                          "rtpsubflow_6", "rtpsubflow_7", "rtpsubflow_8", "rtpsubflow_9", "rtpsubflow_10"]
        result = []
        for subflow_id in range(self._subflows_num):
            forward_flow = {
                "flow_id": self._flow_ids[subflow_id],
                "title": "RTP",
                "fec_title": "RTP + FEC",
                "plot_fec": True,
                "path_ctrler": self.__forward_rtp_flow,
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[subflow_id],
                "subflow_id": subflow_id + 1,
                "evaluations": None,
                "sources": None,
            }
            result.append(forward_flow)
        return result

    def get_plot_description(self):
        result = []
        for subflow_id in range(self._subflows_num):
            result.append({
                "type": "srqmd",
                "plot_id": "subflow_" + str(subflow_id + 1),
                "sr_title": "Sending Rate for Subflow " + str(subflow_id + 1),
                "qmd_title": "Queue delay for Subflow " + str(subflow_id + 1),
                "filename": '_'.join(
                    [self.name, "srqmd", "subflow_" + str(subflow_id), str(self.algorithm.name), str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": self._flow_ids[subflow_id],
                "bandwidths": self._forward_bandwidths[subflow_id],
                "plot_bandwidth": True,
            })
        bandwidths = [0] * len(self._forward_bandwidths[0])
        for subflow_id in range(self._subflows_num):
            for i in range(len(self._forward_bandwidths[0])):
                bandwidths[i] += self._forward_bandwidths[subflow_id][i]

        # print(bandwidths)
        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


class MPRTP9(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header=3, fec_payload_type_id=126,
                 subflows_num=2):
        MyTest.__init__(self, "mprtp9", 120, algorithm, str(latency), str(jitter))

        self._forward_bandwidths = []
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id
        self._subflows_num = subflows_num

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self._flow_ids = []
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                    "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5000, 5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018]
        rtcp_ports = [5001, 5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_9" + "_" + str(self._subflows_num),
                                            path="./",
                                            flownum=1,
                                            codec=Codecs.VP8,
                                            algorithm=self.algorithm,
                                            rtp_ips=rtp_ips[:self._subflows_num],
                                            rtp_ports=rtp_ports[:self._subflows_num],
                                            rtcp_ips=rtcp_ips[:self._subflows_num],
                                            rtcp_ports=rtcp_ports[:self._subflows_num],
                                            start_delay=0,
                                            source_type=self.__source_type,
                                            sink_type=self.__sink_type,
                                            mprtp_ext_header_id=self.__mprtp_ext_header_id,
                                            subflow_ids=subflow_ids[:self._subflows_num]
                                            )

        result = [self.__forward_rtp_flow]
        return result

    def __generate_path_ctrlers(self):
        result = []
        path_names = ["veth2", "veth6", "veth10", "veth14", "veth18", "veth22", "veth26", "veth30", "veth34", "veth38"]
        for subflow_id in range(self._subflows_num):
            flow_stage = [
                {
                    "duration": 120,
                    "config": PathConfig(bandwidth=1000, latency=50 if subflow_id < 1 else 300, jitter=self.__jitter)
                }
            ]
            forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stage))
            forward_path_ctrler = PathShellCtrler(path_name=path_names[subflow_id], path_stage=path_stage)
            self._forward_bandwidths.append(forward_bandwidths)
            result.append(forward_path_ctrler)
        return result

    def __generate_description(self):
        self._flow_ids = ["rtpsubflow_1", "rtpsubflow_2", "rtpsubflow_3", "rtpsubflow_4", "rtpsubflow_5",
                          "rtpsubflow_6", "rtpsubflow_7", "rtpsubflow_8", "rtpsubflow_9", "rtpsubflow_10"]
        result = []
        for subflow_id in range(self._subflows_num):
            forward_flow = {
                "flow_id": self._flow_ids[subflow_id],
                "title": "RTP",
                "fec_title": "RTP + FEC",
                "plot_fec": True,
                "path_ctrler": self.__forward_rtp_flow,
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[subflow_id],
                "subflow_id": subflow_id + 1,
                "evaluations": None,
                "sources": None,
            }
            result.append(forward_flow)
        return result

    def get_plot_description(self):
        result = []
        for subflow_id in range(self._subflows_num):
            result.append({
                "type": "srqmd",
                "plot_id": "subflow_" + str(subflow_id + 1),
                "sr_title": "Sending Rate for Subflow " + str(subflow_id + 1),
                "qmd_title": "Queue delay for Subflow " + str(subflow_id + 1),
                "filename": '_'.join(
                    [self.name, "srqmd", "subflow_" + str(subflow_id), str(self.algorithm.name),
                     str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": self._flow_ids[subflow_id],
                "bandwidths": self._forward_bandwidths[subflow_id],
                "plot_bandwidth": True,
            })
        bandwidths = [0] * len(self._forward_bandwidths[0])
        for subflow_id in range(self._subflows_num):
            for i in range(len(self._forward_bandwidths[0])):
                bandwidths[i] += self._forward_bandwidths[subflow_id][i]

        # print(bandwidths)
        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": self._flow_ids[:self._subflows_num],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


class MPRTP10(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header=3, fec_payload_type_id=126,
                 subflows_num=2, tcp=False):
        MyTest.__init__(self, "mprtp10", 120, algorithm, str(latency), str(jitter))

        self._forward_bandwidths = []
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id
        self._subflows_num = subflows_num

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self._tcp = tcp
        self._flow_ids = []
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                    "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5000, 5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018]
        rtcp_ports = [5001, 5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_10_1" + "_" + str(self._subflows_num),
                                            path="./",
                                            flownum=1,
                                            codec=Codecs.VP8,
                                            algorithm=self.algorithm,
                                            rtp_ips=["10.0.0.6", "10.0.1.6"],
                                            rtp_ports=[5000, 5002],
                                            rtcp_ips=["10.0.0.1", "10.0.1.1"],
                                            rtcp_ports=[5001, 5003],
                                            start_delay=0,
                                            source_type=self.__source_type,
                                            sink_type=self.__sink_type,
                                            mprtp_ext_header_id=self.__mprtp_ext_header_id,
                                            subflow_ids=[1, 2]
                                            )

        self.__forward_rtp_flow_2 = MPRTPFlow(name="mprtpflow_10_2" + "_" + str(self._subflows_num),
                                            path="./",
                                            flownum=2,
                                            codec=Codecs.VP8,
                                            algorithm=self.algorithm,
                                            rtp_ips=["10.0.1.6", "10.0.2.6"],
                                            rtp_ports=[5004, 5006],
                                            rtcp_ips=["10.0.1.1", "10.0.2.1"],
                                            rtcp_ports=[5005, 5007],
                                            start_delay=0,
                                            source_type=self.__source_type,
                                            sink_type=self.__sink_type,
                                            mprtp_ext_header_id=self.__mprtp_ext_header_id,
                                            subflow_ids=[1, 2]
                                            )

        result = [self.__forward_rtp_flow, self.__forward_rtp_flow_2]
        return result

    def __generate_path_ctrlers(self):
        result = []
        path_names = ["veth2", "veth6", "veth10", "veth14", "veth18", "veth22", "veth26", "veth30", "veth34", "veth38"]
        for subflow_id in range(3):
            flow_stage = [
                {
                    "duration": 120,
                    "config": PathConfig(bandwidth=1000, latency=self.__latency, jitter=self.__jitter)
                }
            ]
            forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stage))
            forward_path_ctrler = PathShellCtrler(path_name=path_names[subflow_id], path_stage=path_stage)
            self._forward_bandwidths.append(forward_bandwidths)
            result.append(forward_path_ctrler)
        return result

    def __generate_description(self):
        result = [{
                "flow_id": "mprtpflow_1_flow_1_path_1",
                "title": "MPRTP 1 Subflow 1",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__path_ctrlers[0],
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[0],
                "subflow_id": 1,
                "evaluations": None,
                "sources": None,
            },
            {
                "flow_id": "mprtpflow_1_flow_2_path_2",
                "title": "MPRTP 1 Subflow 2",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__path_ctrlers[1],
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[1],
                "subflow_id": 2,
                "evaluations": None,
                "sources": None,
            },

            {
                "flow_id": "mprtpflow_2_flow_1_path_2",
                "title": "MPRTP 2 Subflow 1",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__path_ctrlers[1],
                "flow": self.__forward_rtp_flow_2,
                "bandwidths": self._forward_bandwidths[1],
                "subflow_id": 1,
                "evaluations": None,
                "sources": None,
            },

            {
                "flow_id": "mprtpflow_2_flow_2_path_3",
                "title": "MPRTP 2 Subflow 2",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__path_ctrlers[2],
                "flow": self.__forward_rtp_flow_2,
                "bandwidths": self._forward_bandwidths[2],
                "subflow_id": 2,
                "evaluations": None,
                "sources": None,
            },
        ]
        return result

    def get_plot_description(self):
        result = [{
                "type": "srqmd",
                "plot_id": "path_1",
                "sr_title": "Sending Rate for Path 1" ,
                "qmd_title": "Queue delay for Path 1",
                "filename": '_'.join(
                    [self.name, "srqmd", "path_1", str(self.algorithm.name),
                     str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": ["mprtpflow_1_flow_1_path_1"],
                "bandwidths": self._forward_bandwidths[0],
                "plot_bandwidth": False,
            },
            {
                "type": "srqmd",
                "plot_id": "path_2",
                "sr_title": "Sending Rate for Path 2",
                "qmd_title": "Queue delay for Path 2",
                "filename": '_'.join(
                    [self.name, "srqmd", "path_2", str(self.algorithm.name),
                     str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": ["mprtpflow_1_flow_2_path_2", "mprtpflow_2_flow_1_path_2"],
                "bandwidths": self._forward_bandwidths[1],
                "plot_bandwidth": False,
            },
            {
                "type": "srqmd",
                "plot_id": "path_3",
                "sr_title": "Sending Rate for Path 3",
                "qmd_title": "Queue delay for Path 3",
                "filename": '_'.join(
                    [self.name, "srqmd", "path_3", str(self.algorithm.name),
                     str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": ["mprtpflow_2_flow_2_path_3"],
                "bandwidths": self._forward_bandwidths[2],
                "plot_bandwidth": False,
            }
        ]

        bandwidths = [0] * len(self._forward_bandwidths[0])
        for subflow_id in range(2):
            for i in range(len(self._forward_bandwidths[0])):
                bandwidths[i] += self._forward_bandwidths[subflow_id][i]

        # print(bandwidths)
        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), "flow_1", str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": ["mprtpflow_1_flow_1_path_1", "mprtpflow_1_flow_2_path_2"],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })

        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), "flow_2", str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": ["mprtpflow_2_flow_1_path_2", "mprtpflow_2_flow_2_path_3"],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description


class MPRTP11(MyTest):
    def __init__(self, algorithm, latency, jitter, source_type, sink_type, mprtp_ext_header=3, fec_payload_type_id=126,
                 subflows_num=2, tcp=False):
        MyTest.__init__(self, "mprtp11", 120, algorithm, str(latency), str(jitter))

        self._forward_bandwidths = []
        self.__algorithm = algorithm
        self.__latency = latency
        self.__jitter = jitter
        self.__source_type = source_type
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header
        self.__fec_payload_type_id = fec_payload_type_id
        self._subflows_num = subflows_num

        self._tcp = tcp
        self.__tcp_packetlog = "tcp_packets_1.pcap"
        self.__tcp_packetlog_2 = "tcp_packets_1.pcap"

        self.__forward_bandwidths_1 = None
        self.__forward_path_ctrler_1 = None
        self.__forward_bandwidths_2 = None
        self.__forward_path_ctrler_2 = None
        self.__forward_rtp_flow = None

        self._flow_ids = []
        self.__flows = self.__generate_flows()
        self.__path_ctrlers = self.__generate_path_ctrlers()
        self.__description = self.__generate_description()

    def __generate_flows(self):
        rtp_ips = ["10.0.0.6", "10.0.1.6", "10.0.2.6", "10.0.3.6", "10.0.4.6", "10.0.5.6", "10.0.6.6", "10.0.7.6",
                   "10.0.8.6", "10.0.9.6"]
        rtcp_ips = ["10.0.0.1", "10.0.1.1", "10.0.2.1", "10.0.3.1", "10.0.4.1", "10.0.5.1", "10.0.6.1", "10.0.7.1",
                    "10.0.8.1", "10.0.9.1"]
        rtp_ports = [5000, 5002, 5004, 5006, 5008, 5010, 5012, 5014, 5016, 5018]
        rtcp_ports = [5001, 5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019]
        subflow_ids = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
        self.__forward_rtp_flow = MPRTPFlow(name="mprtpflow_11_1" + "_" + str(self._subflows_num),
                                            path="./",
                                            flownum=1,
                                            codec=Codecs.VP8,
                                            algorithm=self.algorithm,
                                            rtp_ips=["10.0.0.6", "10.0.1.6"],
                                            rtp_ports=[5000, 5002],
                                            rtcp_ips=["10.0.0.1", "10.0.1.1"],
                                            rtcp_ports=[5001, 5003],
                                            start_delay=0,
                                            source_type=self.__source_type,
                                            sink_type=self.__sink_type,
                                            mprtp_ext_header_id=self.__mprtp_ext_header_id,
                                            subflow_ids=[1,2]
                                            )

        self.__forward_rtp_flow_2 = MPRTPFlow(name="mprtpflow_11_2" + "_" + str(self._subflows_num),
                                            path="./",
                                            flownum=2,
                                            codec=Codecs.VP8,
                                            algorithm=self.algorithm,
                                            rtp_ips=["10.0.0.6", "10.0.1.6"],
                                            rtp_ports=[5004, 5006],
                                            rtcp_ips=["10.0.0.1", "10.0.1.1"],
                                            rtcp_ports=[5005, 5007],
                                            start_delay=0,
                                            source_type=self.__source_type,
                                            sink_type=self.__sink_type,
                                            mprtp_ext_header_id=self.__mprtp_ext_header_id,
                                            subflow_ids=[1,2]
                                            )

        result = [self.__forward_rtp_flow, self.__forward_rtp_flow_2]
        return result

    def __generate_path_ctrlers(self):
        result = []
        path_names = ["veth2", "veth6", "veth10", "veth14", "veth18", "veth22", "veth26", "veth30", "veth34", "veth38"]
        for subflow_id in range(2):
            flow_stage = [
                {
                    "duration": 120,
                    "config": PathConfig(bandwidth=1000, latency=self.__latency, jitter=self.__jitter)
                }
            ]
            forward_bandwidths, path_stage = self.make_bandwidths_and_path_stage(deque(flow_stage))
            forward_path_ctrler = PathShellCtrler(path_name=path_names[subflow_id], path_stage=path_stage)
            self._forward_bandwidths.append(forward_bandwidths)
            result.append(forward_path_ctrler)
        return result

    def __generate_description(self):
        result = [{
                "flow_id": "mprtpflow_1_flow_1_path_1",
                "title": "MPRTP 1 Subflow 1",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__path_ctrlers[0],
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[0],
                "subflow_id": 1,
                "evaluations": None,
                "sources": None,
            },
            {
                "flow_id": "mprtpflow_1_flow_2_path_2",
                "title": "MPRTP 1 Subflow 2",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__path_ctrlers[0],
                "flow": self.__forward_rtp_flow,
                "bandwidths": self._forward_bandwidths[0],
                "subflow_id": 2,
                "evaluations": None,
                "sources": None,
            },

            {
                "flow_id": "mprtpflow_2_flow_1_path_1",
                "title": "MPRTP 2 Subflow 1",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__path_ctrlers[1],
                "flow": self.__forward_rtp_flow_2,
                "bandwidths": self._forward_bandwidths[1],
                "subflow_id": 1,
                "evaluations": None,
                "sources": None,
            },

            {
                "flow_id": "mprtpflow_2_flow_2_path_2",
                "title": "MPRTP 2 Subflow 2",
                "fec_title": "RTP + FEC",
                "plot_fec": False,
                "path_ctrler": self.__path_ctrlers[1],
                "flow": self.__forward_rtp_flow_2,
                "bandwidths": self._forward_bandwidths[1],
                "subflow_id": 2,
                "evaluations": None,
                "sources": None,
            },
        ]
        return result

    def get_plot_description(self):
        result = [{
                "type": "srqmd",
                "plot_id": "path_1",
                "sr_title": "Sending Rate for Path 1" ,
                "qmd_title": "Queue delay for Path 1",
                "filename": '_'.join(
                    [self.name, "srqmd", "path_1", str(self.algorithm.name),
                     str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": ["mprtpflow_1_flow_1_path_1",  "mprtpflow_2_flow_1_path_1"],
                "bandwidths": self._forward_bandwidths[0],
                "plot_bandwidth": False,
            },
            {
                "type": "srqmd",
                "plot_id": "path_2",
                "sr_title": "Sending Rate for Path 2",
                "qmd_title": "Queue delay for Path 2",
                "filename": '_'.join(
                    [self.name, "srqmd", "path_2", str(self.algorithm.name),
                     str(self.latency) + "ms",
                     str(self.jitter) + "ms"]),
                "flow_ids": ["mprtpflow_1_flow_2_path_2", "mprtpflow_2_flow_2_path_2"],
                "bandwidths": self._forward_bandwidths[1],
                "plot_bandwidth": False,
            }
        ]

        bandwidths = [0] * len(self._forward_bandwidths[0])
        for subflow_id in range(2):
            for i in range(len(self._forward_bandwidths[0])):
                bandwidths[i] += self._forward_bandwidths[subflow_id][i]

        # print(bandwidths)
        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), "flow_1", str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": ["mprtpflow_1_flow_1_path_1", "mprtpflow_1_flow_2_path_2"],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })

        result.append({
            "type": "aggr",
            "plot_id": "aggregated",
            "sr_title": "Aggregated Sending Rate",  # sending rates
            "disr_title": "Ratio between Subflows",  # distribution of the sending rates
            "filename": '_'.join([self.name, "aggr", str(self.algorithm.name), "flow_2", str(self.latency) + "ms",
                                  str(self.jitter) + "ms"]),
            "flow_ids": ["mprtpflow_2_flow_1_path_1", "mprtpflow_2_flow_2_path_2"],
            "plot_bandwidth": True,
            "bandwidths": bandwidths,
            "plot_fec": True,
            "fec_title": "RTP + FEC",
            "title": "RTP"
        })
        return result

    def get_flows(self):
        return self.__flows

    def get_path_ctrlers(self):
        return self.__path_ctrlers

    def get_descriptions(self):
        return self.__description
