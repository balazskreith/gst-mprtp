class Actor:
    """
    Represent an actor participate and actively influence the test

    All Inherited class should define the corresponding methods
    """
    def __init__(self):
        pass


class RTPReceiver(Actor):
    """
    Represent an RTPReceiver
    """
    def __init__(self, path, codec, algorithm, rtp_port, rtcp_ip, rtcp_port):
        """
        Parameters:
        -----------
        path : str
            the location of the program controls this actor
        codec : Codec
            The codec used for decoding the stream
        algorithm : Algorithms
            The congestion control algorithm used for reporting RTCP to the sender
        rtp_port : int
            The port bind to listen incoming rtp TrafficUnit
        rtcp_ip : str
            The IP address used for sending outgoing RTCP traffic
        rtcp_port : int
            The RTCP port aimed to send RTCP traffic on the other side
        """
        Actor.__init__(self)
        self.__path = path
        self.__codec = codec
        self.__algorithm = algorithm
        self.__rtp_port = rtp_port
        self.__rtcp_ip = rtcp_ip
        self.__rtcp_port = rtcp_port

    @property
    def path(self):
        """Get the Path property"""
        return self.__path

    @property
    def codec(self):
        """Get the Codec property"""
        return self.__codec

    @property
    def algorithm(self):
        """Get the Algorithm property"""
        return self.__algorithm

    @property
    def rtcp_port(self):
        """Get the RTCP port property"""
        return self.__rtcp_port

    @property
    def rtcp_ip(self):
        """Get the RTCP IP property"""
        return self.__rtcp_ip

    @property
    def rtp_port(self):
        """Get the RTP IP property"""
        return self.__rtp_port


class RTPSender(Actor):
    def __init__(self, path, codec, algorithm, rtcp_port, rtp_ip, rtp_port):
        Actor.__init__(self)
        self.__path = path
        self.__codec = codec
        self.__algorithm = algorithm
        self.__rtcp_port = rtcp_port
        self.__rtp_ip = rtp_ip
        self.__rtp_port = rtp_port

    @property
    def path(self):
        """Get the Path property"""
        return self.__path

    @property
    def codec(self):
        """Get the Codec property"""
        return self.__codec

    @property
    def algorithm(self):
        """Get the Algorithm property"""
        return self.__algorithm

    @property
    def rtcp_port(self):
        """Get the RTCP Port property"""
        return self.__rtcp_port

    @property
    def rtp_ip(self):
        """Get the RTP IP property"""
        return self.__rtp_ip

    @property
    def rtp_port(self):
        """Get the RTP Port property"""
        return self.__rtp_port

class TCPServer(Actor):
    pass

class TCPClient(Actor):
    pass
