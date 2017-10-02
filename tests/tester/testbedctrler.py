from command import Command
from commander import *
from pushers import *
import os

class TestBedCtrler:
    """
    Represent controller for the testbed
    """
    def __init__(self, testbed_cmd_input, source_cmd_input, midbox_cmd_input, sink_cmd_input):
        """
        Parameters
        ----------
        testbed_cmd_input : Sink
            A sink for the testbed
        midbox_cmd_input : Sink
            A sink for the midbox
        source_cmd_input : Sink
            A sink for the test source
        sink_cmd_input : Sink
            A sink for the testbed sink
        """
        self.__testbed_cmd_input = testbed_cmd_input
        self.__source_cmd_input = source_cmd_input
        self.__midbox_cmd_input = midbox_cmd_input
        self.__sink_cmd_input = sink_cmd_input

    @property
    def testbed_cmd_input(self):
        """Gets the testbed command input sink property"""
        return self.__testbed_cmd_input

    @property
    def source_cmd_input(self):
        """Gets the testbed source command input sink property"""
        return self.__source_cmd_input

    @property
    def midbox_cmd_input(self):
        """Gets the testbed midbox command input sink property"""
        return self.__midbox_cmd_input

    @property
    def sink_cmd_input(self):
        """Gets the sink source command input sink property"""
        return self.__sink_cmd_input

class LinuxTestBedCtrler(TestBedCtrler):
    def __init__(self, source_namespace = "ns_snd", midbox_namespace = "ns_mid", sink_namespace = "ns_rcv",
        source_output = None, middlebox_output = None, sink_output = None):
        self.__shell = ShellCommander()
        self.__source_commander = LinuxNamespaceCommander(source_namespace, source_output, source_output)
        source_cmd_input = Sink(self.__source_commander.execute)

        self.__midbox_commander = LinuxNamespaceCommander(midbox_namespace, middlebox_output, middlebox_output)
        midbox_cmd_input = Sink(self.__midbox_commander.execute)

        self.__sink_commander = LinuxNamespaceCommander(sink_namespace, sink_output, sink_output)
        sink_cmd_input = Sink(self.__sink_commander.execute)
        TestBedCtrler.__init__(self, None, source_cmd_input, midbox_cmd_input, sink_cmd_input)
