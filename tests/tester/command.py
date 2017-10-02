import logging

class Command:
    def __init__(self, message, stdout = logging.debug, stderr = logging.debug):
        """
        Parameters:
        -----------
        message : str
            The message will be inetrpreted as a command
        stdout : object
            the
        algorithm : Algorithms
            The congestion control algorithm used for reporting RTCP to the sender
        rtp_port : int
            The port bind to listen incoming rtp TrafficUnit
        rtcp_ip : str
            The IP address used for sending outgoing RTCP traffic
        rtcp_port : int
            The RTCP port aimed to send RTCP traffic on the other side
        """
        self.__message = message
        self.__stdout = stdout
        self.__stderr = stderr

    def logging(self, message):
        logging.debug(message.decode("utf-8") if type(message) is bytes else message)

    @property
    def message(self):
        """Get the Message property"""
        return self.__message

    @property
    def stdout(self):
        """Get the stdout property"""
        return self.__stdout

    @property
    def stderr(self):
        """Get the stderr property"""
        return self.__stderr

    def to_bytes(self):
        """Get the to_bytes property"""
        raise NotImplementedError

    def __str__(self):
        """Get the human readable format of the command"""
        return str(self.__message)

class ShellCommand(Command):
    def __init__(self, message, stdout = logging.debug, stderr = logging.debug):
        Command.__init__(self, message, stdout, stderr)
        # print(message)

    def to_bytes(self):
        """Get the message in bytes"""
        return bytearray(str(self.message), 'utf-8')
