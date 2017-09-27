from trafficunits import *

class Flow:
    """
    Represent a flow contains a source and sink part
    """
    def __init__(self, source_unit, sink_unit, sink_to_source_delay = 0, start_delay = 0):
        """
        Parameters:
        -----------
        source_unit : TrafficUnit
            The source traffic unit used as a source
        sink_unit : TrafficUnit
            The sink traffic unit used as a sink
        sink_to_source_delay : int
            A delay between the sink and source enforced during the start
        start_delay : int
            A delay enforced before the flow is being started
        """
        self.__source_unit = source_unit
        self.__sink_unit = sink_unit
        self.__sink_to_source_delay = sink_to_source_delay
        self.__start_delay = start_delay

    @property
    def source_unit(self):
        """Get the source unit property"""
        return self.__source_unit

    @property
    def sink_unit(self):
        """Get the sink unit property"""
        return self.__sink_unit

    @property
    def sink_to_source_delay(self):
        """Get the sink to source delay property"""
        return self.__sink_to_source_delay

    @property
    def start_delay(self):
        """Get the start delay property"""
        return self.__start_delay

class RTPFlow(Flow):
    """
    Represent an RTP Flow

    """
    def __init__(self, path, codec, algorithm, rtp_ip, rtp_port, rtcp_ip, rtcp_port, start_delay = 0,
    source_type = "FILE:foreman_cif.yuv:1:352:288:2:25/1", sink_type = "FAKESINK", mprtp_ext_header_id = 0):
        """
        Init the parameter for the test

        Parameters
        ----------
        path : string
            The folder where snd and rcv pipeline are found
        codec : Codecs
            The codec used for en/decoding the stream
        algorithm : Algorithms
            The algorithm used for congestion controlling
        rtp_ip : str
            The IP the RTP traffic is sent to / listened from
        rtp_port : int
            The port the RTP traffic is sent to / listened from
        rtcp_ip : str
            The IP the RTCP traffic is sent to / listened from
        rtcp_port : int
            The port the RTCP traffic is sent to / listened from
        start : int
            Indicate the start delay from the moment it is started
        """
        rtp_sender = RTPSenderShellTrafficUnit(path=path + "snd_pipeline", codec=codec,
            algorithm=algorithm, rtcp_port=rtcp_port, rtp_ip=rtp_ip, rtp_port=rtp_port, snd_stat="snd_packets_1.csv",
            source_type=source_type, mprtp_ext_header_id=mprtp_ext_header_id)

        rtp_receiver = RTPReceiverShellTrafficUnit(path=path + "rcv_pipeline", codec=codec,
            algorithm=algorithm, rtp_port=rtp_port, rtcp_ip=rtcp_ip, rtcp_port=rtcp_port,
            rcv_stat="rcv_packets_1.csv", ply_stat="ply_packets_1.csv",
            sink_type=sink_type, mprtp_ext_header_id=mprtp_ext_header_id)
        Flow.__init__(self, rtp_sender, rtp_receiver, 2, start_delay)

    def __str__(self):
        """Get the human readable format of the object"""
        units = [" ", str(self.source_unit), str(self.sink_unit)]
        return "RTPFlow: " + "\n".join(units)
