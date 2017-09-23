from trafficunits import *

class Flow:
    def __init__(self, source_unit, sink_unit, sink_to_source_delay = 0, start_delay = 0):
        self.__source_unit = source_unit
        self.__sink_unit = sink_unit
        self.__sink_to_source_delay = sink_to_source_delay
        self.__start_delay = start_delay

    @property
    def source_unit(self):
        return self.__source_unit

    @property
    def sink_unit(self):
        return self.__sink_unit

    @property
    def sink_to_source_delay(self):
        return self.__sink_to_source_delay

    @property
    def start_delay(self):
        return self.__start_delay

class RTPFlow(Flow):
    """
    Represent an RTP Flow

    """
    def __init__(self, path, codec, algorithm, rtp_ip, rtp_port, rtcp_ip, rtcp_port, start_delay = 0):
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
            algorithm=algorithm, rtcp_port=rtcp_port, rtp_ip=rtp_ip, rtp_port=rtp_port, snd_stat="snd_packets_1.csv")
        rtp_receiver = RTPReceiverShellTrafficUnit(path=path + "rcv_pipeline", codec=codec,
            algorithm=algorithm, rtp_port=rtp_port, rtcp_ip=rtcp_ip, rtcp_port=rtcp_port,
            rcv_stat="rcv_packets_1.csv", ply_stat="ply_packets_1.csv")
        Flow.__init__(self, rtp_sender, rtp_receiver, 2, start_delay)
