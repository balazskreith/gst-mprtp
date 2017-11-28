from trafficunits import *

class Flow:
    """
    Represent a flow contains a source and sink part
    """
    def __init__(self, source_unit, sink_unit, sink_to_source_delay = 0, start_delay = 0,
        source_program_name = None, sink_program_name = None, flipped = False, packetlogs = [], outputlogs = [],
        subflow_ids = None, title = "Unknown"):
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
        self.__source_program_name = source_program_name
        self.__sink_program_name = sink_program_name
        self.__flipped = flipped
        self.__packetlogs = packetlogs
        self.__outputlogs = outputlogs
        self.__subflow_ids = subflow_ids

    @property
    def sink_program_name(self):
        """Get the sink_program_name property"""
        return self.__sink_program_name

    @property
    def source_program_name(self):
        """Get the sink_program_name property"""
        return self.__source_program_name

    @property
    def subflow_ids(self):
        """Get the packetlogs property"""
        return self.__subflow_ids

    @property
    def packetlogs(self):
        """Get the packetlogs property"""
        return self.__packetlogs

    @property
    def outputlogs(self):
        """Get the outputlogs property"""
        return self.__outputlogs

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

    @property
    def flipped(self):
        """Get the flipped property"""
        return self.__flipped

class RTPFlow(Flow):
    """
    Represent an RTP Flow

    """
    def __init__(self, name, flownum, path, codec, algorithm, rtp_ip, rtp_port, rtcp_ip, rtcp_port, start_delay = 0,
    source_type = "FILE:foreman_cif.yuv:1:352:288:2:25/1", sink_type = "FAKESINK", mprtp_ext_header_id = 0,
                 flipped = False):
        """
        Init the parameter for the test

        Parameters
        ----------
        name : string
            The name of the flow
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
        flipped : bool
            Indicate for any controller whether the flow sink and source needs to be flipped or not
        """
        self.__name = name
        self.__flownum = flownum
        snd_pipeline = "snd_pipeline"
        rcv_pipeline = "rcv_pipeline"
        snd_packetlog = "snd_packets_" + str(self.__flownum) + ".csv"
        rcv_packetlog = "rcv_packets_" + str(self.__flownum) + ".csv"
        ply_packetlog = "ply_packets_" + str(self.__flownum) + ".csv"
        rtp_sender = RTPSenderShellTrafficUnit(name=name+"-snd", path=path, program_name = snd_pipeline, codec=codec,
            algorithm=algorithm, rtcp_port=rtcp_port, rtp_ip=rtp_ip, rtp_port=rtp_port, snd_stat="/tmp/"+snd_packetlog,
            source_type=source_type, mprtp_ext_header_id=mprtp_ext_header_id)

        rtp_receiver = RTPReceiverShellTrafficUnit(name=name+"-rcv", path=path, program_name = rcv_pipeline, codec=codec,
            algorithm=algorithm, rtp_port=rtp_port, rtcp_ip=rtcp_ip, rtcp_port=rtcp_port,
            rcv_stat="/tmp/"+rcv_packetlog, ply_stat="/tmp/"+ply_packetlog, sink_type=sink_type, mprtp_ext_header_id=mprtp_ext_header_id)

        outputlogs = [rtp_sender.get_logfile(), rtp_receiver.get_logfile()]
        packetlogs = [snd_packetlog, rcv_packetlog, ply_packetlog]
        Flow.__init__(self, rtp_sender, rtp_receiver, sink_to_source_delay = 2, start_delay = start_delay, flipped = flipped,
            outputlogs = outputlogs, packetlogs = packetlogs)

    def __str__(self):
        """Get the human readable format of the object"""
        units = [" ", str(self.source_unit), str(self.sink_unit)]
        return "RTPFlow: " + "\n".join(units)


class MPRTPFlow(Flow):
    """
    Represent an RTP Flow

    """
    def __init__(self, name, flownum, path, codec, algorithm, rtp_ips, rtp_ports, rtcp_ips, rtcp_ports, start_delay = 0,
    source_type = "FILE:foreman_cif.yuv:1:352:288:2:25/1", sink_type = "FAKESINK", mprtp_ext_header_id = 0,
    subflow_ids = None, flipped = False):
        """
        Init the parameter for the test

        Parameters
        ----------
        name : string
            The name of the flow
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
        flipped : bool
            Indicate for any controller whether the flow sink and source needs to be flipped or not
        """
        self.__name = name
        self.__flownum = flownum
        snd_pipeline = "snd_pipeline"
        rcv_pipeline = "rcv_pipeline"
        snd_packetlog = "snd_packets_" + str(self.__flownum) + ".csv"
        rcv_packetlog = "rcv_packets_" + str(self.__flownum) + ".csv"
        ply_packetlog = "ply_packets_" + str(self.__flownum) + ".csv"
        mprtp_sender = MPRTPSenderShellTrafficUnit(name=name+"-snd", path=path, program_name = snd_pipeline, codec=codec,
            algorithm=algorithm, rtcp_ports=rtcp_ports, rtp_ips=rtp_ips, rtp_ports=rtp_ports, snd_stat="/tmp/"+snd_packetlog,
            source_type=source_type, mprtp_ext_header_id=mprtp_ext_header_id)

        mprtp_receiver = MPRTPReceiverShellTrafficUnit(name=name+"-rcv", path=path, program_name=rcv_pipeline, codec=codec,
            algorithm=algorithm, rtp_ports=rtp_ports, rtcp_ips=rtcp_ips, rtcp_ports=rtcp_ports,
            rcv_stat="/tmp/"+rcv_packetlog, ply_stat="/tmp/"+ply_packetlog,
            sink_type=sink_type, mprtp_ext_header_id=mprtp_ext_header_id)

        outputlogs = [mprtp_sender.get_logfile(), mprtp_receiver.get_logfile()]
        packetlogs = [snd_packetlog, rcv_packetlog, ply_packetlog]

        Flow.__init__(self, mprtp_sender, mprtp_receiver, sink_to_source_delay=2, start_delay=start_delay, flipped=flipped,
            outputlogs=outputlogs, packetlogs=packetlogs, subflow_ids=subflow_ids)

    def __str__(self):
        """Get the human readable format of the object"""
        units = [" ", str(self.source_unit), str(self.sink_unit)]
        return "RTPFlow: " + "\n".join(units)


class TCPFlow(Flow):
    """
    Represent an RTP Flow

    """
    def __init__(self, name, server_ip, server_port, tcp_server = None,
                 start_delay = 0, duration = 0, create_client = True, packetlogs = []):
        """
        Init the parameter for the test

        Parameters
        ----------
        name : string
            The name of the flow
        server_ip : str
            The IP the RTCP traffic is sent to / listened from
        server_port : int
            The port the RTCP traffic is sent to / listened from
        """
        self.__name = name
        self.__server_ip = server_ip
        self.__server_port = server_port
        outputlogs = []

        if tcp_server is None:
            self.__tcp_server = TCPServerTrafficShellUnit("TCPServer", server_ip, server_port)
            outputlogs.append(self.__tcp_server.get_logfile())
        else:
            self.__tcp_server = tcp_server

        self.__duration = duration

        tcp_client = None
        if create_client:
            tcp_client = TCPClientTrafficShellUnit("TCPClient", self.__tcp_server, duration)
            outputlogs.append(tcp_client.get_logfile())

        Flow.__init__(self, tcp_client, self.__tcp_server if tcp_server is None else None,
                      sink_to_source_delay = 0, start_delay = start_delay,
                outputlogs = outputlogs, packetlogs = packetlogs)

    @property
    def tcp_server(self):
        return self.__tcp_server

    def __str__(self):
        """Get the human readable format of the object"""
        units = [" ", str(self.source_unit), str(self.sink_unit), "start_delay: " + str(self.start_delay), "duration: " + str(self.__duration)]
        return "TCPlow: " + "\n".join(units)
