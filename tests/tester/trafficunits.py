from actors import *
from constants import *
from command import *

class TrafficUnit:
    def __init__(self):
        pass
    def get_start_cmd(self):
        raise NotImplementedError
    def get_stop_cmd(self):
        raise NotImplementedError
    def get_pause_cmd(self):
        raise NotImplementedError

class RTPReceiverShellTrafficUnit(TrafficUnit, RTPReceiver):
    def __init__(self, path, codec, algorithm, rtp_port, rtcp_ip, rtcp_port, rcv_stat, ply_stat):
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
        RTPReceiver.__init__(self, path, codec, algorithm, rtp_port, rtcp_ip, rtcp_port)
        TrafficUnit.__init__(self)
        self.__rcv_stat = rcv_stat
        self.__ply_stat = ply_stat

    def get_start_cmd(self):
        args = [" "]
        args.append("--codec=" + str(self.codec))
        if (self.algorithm == Algorithms.FRACTaL):
            args.append("--receiver=MPRTP:1:1:" + str(self.rtp_port))
            args.append("--playouter=MPRTPFRACTAL:MPRTP:1:1:" + str(self.rtcp_ip) + ":" + str(self.rtcp_port))
        elif (self.algorithm == Algorithms.SCReAM):
            args.append("--receiver=RTP:" + str(self.rtp_port))
            args.append("--playouter=SCREAM:RTP:" + str(self.rtcp_ip) + ":" + str(self.rtcp_port))
        if (self.__rcv_stat):
            args.append("--stat="+self.__rcv_stat)
        if (self.__ply_stat):
            args.append("--plystat="+self.__ply_stat)

        return ShellCommand(self.path + " ".join(args))

    def get_stop_cmd(self):
        return ShellCommand("ls -a")

    def get_pause_cmd(self):
        return ShellCommand("ls -a")

    def __str__(self):
        infos = [" "]
        infos.append("codec: " + str(self.codec))
        infos.append("algorithm: " + str(self.algorithm))
        infos.append("send RTCP to: " + str(self.rtcp_ip) + ":" + str(self.rtcp_port))
        infos.append("listen RTP at: " + str(self.rtp_port))
        infos.append("save rcv stat at: " + self.__rcv_stat + " ply stat at: " + self.__ply_stat)
        return "RTPReceiverShellTrafficUnit " + " ".join(infos)


class RTPSenderShellTrafficUnit(TrafficUnit, RTPSender):
    def __init__(self, path, codec, algorithm, rtcp_port, rtp_ip, rtp_port, snd_stat):
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
        RTPSender.__init__(self, path, codec, algorithm, rtcp_port, rtp_ip, rtp_port)
        TrafficUnit.__init__(self)
        self.__snd_stat = snd_stat

    def get_start_cmd(self):
        args = [" "]
        args.append("--codec=" + str(self.codec))
        if (self.algorithm == Algorithms.FRACTaL):
            args.append("--sender=MPRTP:1:1:" + str(self.rtp_ip) + ":" + str(self.rtp_port))
            args.append("--scheduler=MPRTPFRACTAL:MPRTP:1:1:" + str(self.rtcp_port))
        elif (self.algorithm == Algorithms.SCReAM):
            args.append("--sender=RTP:" + str(self.rtp_ip) + ":" + str(self.rtp_port))
            args.append("--scheduler=SCREAM:RTP:" + str(self.rtcp_port))
        if (self.__snd_stat):
            args.append("--stat="+self.__snd_stat)

        return ShellCommand(self.path + " ".join(args))

    def get_stop_cmd(self):
        return ShellCommand("ls -a")

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

class TCPServer(Actor):
    pass

class TCPClient(Actor):
    pass
