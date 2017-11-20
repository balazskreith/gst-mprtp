from actors import *
from constants import *
from command import *

class TrafficUnit:
    """Represent a TrafficUnit"""
    def __init__(self):
        pass

    def get_start_cmd(self):
        """"Gets the start command for the unit"""
        raise NotImplementedError

    def get_stop_cmd(self):
        """"Gets the stop command for the unit"""
        raise NotImplementedError

    def get_pause_cmd(self):
        """"Gets the pause command for the unit"""
        raise NotImplementedError

class RTPReceiverShellTrafficUnit(TrafficUnit, RTPReceiver):
    """
    Represent an RTP receiver for shell traffic unit
    """
    def __init__(self, name, path, program_name, codec, algorithm, rtp_port, rtcp_ip, rtcp_port, rcv_stat, ply_stat, sink_type, mprtp_ext_header_id = 0):
        """
        Init the parameter for the test

        Parameters
        ----------
        path : string
            The path where the receiver found in
        codec : Codecs
            The codec used for en/decoding the stream
        algorithm : Algorithms
            The algorithm used for congestion controlling
        rtp_ip : str
            The IP the RTP traffic is sent to / listened from
        rtcp_ip : str
            The IP the RTCP traffic is sent to / listened from
        rtcp_port : int
            The port the RTCP traffic is sent to / listened from
        rcv_stat : str
            Indicate the stats the traffic unit received
        ply_stat : str
            Indicate the stats the traffic unit played out
        """
        RTPReceiver.__init__(self, path + program_name, codec, algorithm, rtp_port, rtcp_ip, rtcp_port)
        TrafficUnit.__init__(self)
        self.__rcv_stat = rcv_stat
        self.__ply_stat = ply_stat
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header_id;
        self.__name = name
        self.__program_name = program_name

    def get_start_cmd(self):
        args = [" "]
        args.append("--codec=" + str(self.codec.name))
        if (self.algorithm == Algorithms.FRACTaL):
            args.append("--receiver=MPRTP:1:1:" + str(self.rtp_port))
            args.append("--playouter=MPRTPFRACTAL:MPRTP:1:1:" + str(self.rtcp_ip) + ":" + str(self.rtcp_port))
        elif (self.algorithm == Algorithms.SCReAM):
            args.append("--receiver=RTP:" + str(self.rtp_port))
            args.append("--playouter=SCREAM:RTP:" + str(self.rtcp_ip) + ":" + str(self.rtcp_port))
        if (self.__rcv_stat):
            args.append("--stat=" + self.__rcv_stat + ":" + str(self.__mprtp_ext_header_id))
        if (self.__ply_stat):
            args.append("--plystat=" + self.__ply_stat + ":" + str(self.__mprtp_ext_header_id))
        args.append("--sink=" + self.__sink_type)

        return ShellCommand(self.path + " ".join(args), stdout = self.logging, stderr = self.logging)

    def get_stop_cmd(self):
        return ShellCommand("pkill " + self.__program_name)

    def get_pause_cmd(self):
        return ShellCommand("ls -a")

    def logging(self, message):
        if message == None:
            return
        with open(self.__name + ".log", "w") as f:
            if isinstance(message, bytes):
                f.write(message.decode("utf-8"))
            else:
                f.write(message)

    def __str__(self):
        infos = [" "]
        infos.append("codec: " + str(self.codec))
        infos.append("algorithm: " + str(self.algorithm))
        infos.append("send RTCP to: " + str(self.rtcp_ip) + ":" + str(self.rtcp_port))
        infos.append("listen RTP at: " + str(self.rtp_port))
        infos.append("save rcv stat at: " + self.__rcv_stat + " ply stat at: " + self.__ply_stat)
        return "RTPReceiverShellTrafficUnit " + " ".join(infos)


class RTPSenderShellTrafficUnit(TrafficUnit, RTPSender):
    """
    Represent an RTP sender for shell traffic unit
    """
    def __init__(self, name, path, program_name, codec, algorithm, rtcp_port, rtp_ip, rtp_port, snd_stat, source_type, mprtp_ext_header_id = 0):
        """
        Init the parameter for the test

        Parameters
        ----------
        path : string
            The path where the receiver found in
        codec : Codecs
            The codec used for en/decoding the stream
        algorithm : Algorithms
            The algorithm used for congestion controlling
        rtcp_port : int
            The port the RTCP traffic is sent to / listened from
        rtp_ip : str
            The IP the RTP traffic is sent to / listened from
        rtcp_port : int
            The IP the RTP traffic is sent to / listened from
        rcv_stat : str
            Indicate the stats the traffic unit received
        ply_stat : str
            Indicate the stats the traffic unit played out
        """
        RTPSender.__init__(self, path + program_name, codec, algorithm, rtcp_port, rtp_ip, rtp_port)
        TrafficUnit.__init__(self)
        self.__program_name = program_name
        self.__snd_stat = snd_stat
        self.__source_type = source_type
        self.__mprtp_ext_header_id = mprtp_ext_header_id
        self.__name = name
        self.__program_name = program_name

    def logging(self, message):
        if message == None:
            return
        with open(self.__name + ".log", "w") as f:
            if isinstance(message, bytes):
                f.write(message.decode("utf-8"))
            else:
                f.write(message)

    def get_start_cmd(self):
        args = [" "]
        args.append("--codec=" + str(self.codec.name))
        if (self.algorithm == Algorithms.FRACTaL):
            args.append("--sender=MPRTP:1:1:" + str(self.rtp_ip) + ":" + str(self.rtp_port))
            args.append("--scheduler=MPRTPFRACTAL:MPRTP:1:1:" + str(self.rtcp_port))
        elif (self.algorithm == Algorithms.SCReAM):
            args.append("--sender=RTP:" + str(self.rtp_ip) + ":" + str(self.rtp_port))
            args.append("--scheduler=SCREAM:RTP:" + str(self.rtcp_port))
        if (self.__snd_stat):
            args.append("--stat=" + self.__snd_stat + ":" + str(self.__mprtp_ext_header_id))
        args.append("--source=" + str(self.__source_type))

        return ShellCommand(self.path + " ".join(args), stdout = self.logging, stderr = self.logging)

    def get_stop_cmd(self):
        return ShellCommand("pkill " + self.__program_name)

    def get_pause_cmd(self):
        return ShellCommand("ls -a")

    def __str__(self):
        infos = [" "]
        infos.append("codec: " + str(self.codec))
        infos.append("algorithm: " + str(self.algorithm))
        infos.append("send RTP to: " + str(self.rtp_ip) + ":" + str(self.rtp_port))
        infos.append("listen RTCP at: " + str(self.rtcp_port))
        infos.append("save snd stat at: "+self.__snd_stat)
        return "RTPSenderShellTrafficUnit " + " ".join(infos)


class MPRTPReceiverShellTrafficUnit(TrafficUnit, RTPReceiver):
    """
    Represent an RTP receiver for shell traffic unit
    """
    def __init__(self, name, path, program_name, codec, algorithm, rtp_ports, rtcp_ips, rtcp_ports, rcv_stat, ply_stat, sink_type, mprtp_ext_header_id = 0):
        """
        Init the parameter for the test

        Parameters
        ----------
        path : string
            The path where the receiver found in
        codec : Codecs
            The codec used for en/decoding the stream
        algorithm : Algorithms
            The algorithm used for congestion controlling
        rtp_ip : str
            The IP the RTP traffic is sent to / listened from
        rtcp_ip : str
            The IP the RTCP traffic is sent to / listened from
        rtcp_port : int
            The port the RTCP traffic is sent to / listened from
        rcv_stat : str
            Indicate the stats the traffic unit received
        ply_stat : str
            Indicate the stats the traffic unit played out
        """
        RTPReceiver.__init__(self, path + program_name, codec, algorithm, rtp_ports, rtcp_ips, rtcp_ports)
        TrafficUnit.__init__(self)
        self.__rcv_stat = rcv_stat
        self.__ply_stat = ply_stat
        self.__sink_type = sink_type
        self.__mprtp_ext_header_id = mprtp_ext_header_id;
        self.__name = name
        self.__program_name = program_name

    def get_start_cmd(self):
        args = [" "]
        args.append("--codec=" + str(self.codec.name))
        args.append("--receiver=MPRTP:1:1:" + str(self.rtp_port))
        args.append("--playouter=MPRTPFRACTAL:MPRTP:1:1:" + str(self.rtcp_ip) + ":" + str(self.rtcp_port))

        for subflow_id in range(1,subflows_num+1):
            receiver += str(subflow_id) + ":" + str(self.rtp_port[subflow_id-1])
            playouter += str(subflow_id) + ":" + str(self.rtcp_ip[subflow_id-1]) + + ":" + str(self.rtcp_port[subflow_id-1])

        args.append(receiver)
        args.append(scheduler)

        if (self.__rcv_stat):
            args.append("--stat=" + self.__rcv_stat + ":" + str(self.__mprtp_ext_header_id))
        if (self.__ply_stat):
            args.append("--plystat=" + self.__ply_stat + ":" + str(self.__mprtp_ext_header_id))
        args.append("--sink=" + self.__sink_type)

        return ShellCommand(self.path + " ".join(args), stdout = self.logging, stderr = self.logging)

    def get_stop_cmd(self):
        return ShellCommand("pkill " + self.__program_name)

    def get_pause_cmd(self):
        return ShellCommand("ls -a")

    def logging(self, message):
        if message == None:
            return
        with open(self.__name + ".log", "w") as f:
            if isinstance(message, bytes):
                f.write(message.decode("utf-8"))
            else:
                f.write(message)

    def __str__(self):
        infos = [" "]
        infos.append("codec: " + str(self.codec))
        infos.append("algorithm: " + str(self.algorithm))
        infos.append("send RTCP to: " + str(self.rtcp_ip) + ":" + str(self.rtcp_port))
        infos.append("listen RTP at: " + str(self.rtp_port))
        infos.append("save rcv stat at: " + self.__rcv_stat + " ply stat at: " + self.__ply_stat)
        return "MPRTPReceiverShellTrafficUnit " + " ".join(infos)


class MPRTPSenderShellTrafficUnit(TrafficUnit, RTPSender):
    """
    Represent an RTP sender for shell traffic unit
    """
    def __init__(self, name, path, program_name, codec, algorithm, rtcp_ports, rtp_ips, rtp_ports, snd_stat, source_type, mprtp_ext_header_id = 0):
        """
        Init the parameter for the test

        Parameters
        ----------
        path : string
            The path where the receiver found in
        codec : Codecs
            The codec used for en/decoding the stream
        algorithm : Algorithms
            The algorithm used for congestion controlling
        rtcp_port : int
            The port the RTCP traffic is sent to / listened from
        rtp_ip : str
            The IP the RTP traffic is sent to / listened from
        rtcp_port : int
            The IP the RTP traffic is sent to / listened from
        rcv_stat : str
            Indicate the stats the traffic unit received
        ply_stat : str
            Indicate the stats the traffic unit played out
        """
        RTPSender.__init__(self, path + program_name, codec, algorithm, rtcp_ports, rtp_ips, rtp_ports)
        TrafficUnit.__init__(self)
        self.__program_name = program_name
        self.__snd_stat = snd_stat
        self.__source_type = source_type
        self.__mprtp_ext_header_id = mprtp_ext_header_id
        self.__name = name
        self.__program_name = program_name

    def logging(self, message):
        if message == None:
            return
        with open(self.__name + ".log", "w") as f:
            if isinstance(message, bytes):
                f.write(message.decode("utf-8"))
            else:
                f.write(message)

    def get_start_cmd(self):
        args = [" "]
        args.append("--codec=" + str(self.codec.name))
        subflows_num = len(self.rtp_ip)
        sender = "--sender=MPRTP:" + str(subflow_num) + ":"
        scheduler = "--scheduler=MPRTPFRACTAL:MPRTP:" + str(subflow_num) + ":"
        for subflow_id in range(1,subflows_num+1):
            sender += str(subflow_id) + ":" + str(self.rtp_ip[subflow_id-1]) + + ":" + str(self.rtp_port[subflow_id-1])
            scheduler += str(subflow_id) + ":" + str(self.rtcp_port[subflow_id-1])

        args.append(sender)
        args.append(scheduler)
        if (self.__snd_stat):
            args.append("--stat=" + self.__snd_stat + ":" + str(self.__mprtp_ext_header_id))
        args.append("--source=" + str(self.__source_type))

        return ShellCommand(self.path + " ".join(args), stdout = self.logging, stderr = self.logging)

    def get_stop_cmd(self):
        return ShellCommand("pkill " + self.__program_name)

    def get_pause_cmd(self):
        return ShellCommand("ls -a")

    def __str__(self):
        infos = [" "]
        infos.append("codec: " + str(self.codec))
        infos.append("algorithm: " + str(self.algorithm))
        infos.append("send RTP to: " + str(self.rtp_ip) + ":" + str(self.rtp_port))
        infos.append("listen RTCP at: " + str(self.rtcp_port))
        infos.append("save snd stat at: "+self.__snd_stat)
        return "MPRTPSenderShellTrafficUnit " + " ".join(infos)

class TCPServerTrafficShellUnit(TrafficUnit, TCPServer):
    """
    Represent a TCP server for shell traffic unit
    """
    def __init__(self, name, ip, port):
        TCPServer.__init__(self, ip, port)
        TrafficUnit.__init__(self)
        self.__name = name

    def logging(self, message):
        if message == None:
            return
        with open(self.__name + ".log", "w") as f:
            if isinstance(message, bytes):
                f.write(message.decode("utf-8"))
            else:
                f.write(message)

    def get_start_cmd(self):
        args = [" "]
        args.append("-s ")
        args.append("-p " + str(self.port))
        return ShellCommand("iperf " + ' '.join(args), stdout = self.logging, stderr = self.logging)

    def get_stop_cmd(self):
        return ShellCommand("pkill iperf")

    def get_pause_cmd(self):
        return ShellCommand("ls -a")

    def __str__(self):
        infos = [" "]
        infos.append("IP: " + str(self.ip))
        infos.append("Port: " + str(self.port))
        return "TCPServer " + " ".join(infos)

class TCPClientTrafficShellUnit(TrafficUnit, TCPClient):
    """
    Represent a TCP server for shell traffic unit
    """
    def __init__(self, name, tcp_server, duration = 0):
        TrafficUnit.__init__(self)
        TCPClient.__init__(self, tcp_server)
        self.__name = name
        self.__duration = duration

    def logging(self, message):
        if message == None:
            return
        with open(self.__name + ".log", "w") as f:
            if isinstance(message, bytes):
                f.write(message.decode("utf-8"))
            else:
                f.write(message)

    def get_start_cmd(self):
        tcp_server = self.tcp_server
        args = [" "]
        args.append("-c " + str(tcp_server.ip))
        args.append("-p " + str(tcp_server.port))
        if (0 < self.__duration):
            args.append("-t " + str(self.__duration))
        return ShellCommand("iperf " + ' '.join(args), stdout = self.logging, stderr = self.logging)

    def get_stop_cmd(self):
        return ShellCommand("pkill iperf")

    def get_pause_cmd(self):
        return ShellCommand("ls -a")

    def __str__(self):
        return "TCPClient connected to " + str(self.tcp_server)
