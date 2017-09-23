from command import Command
from commander import *
from pushers import *
import os

class TestBedCtrler:
    def __init__(self, testbed_cmd_input, source_cmd_input, midbox_cmd_input, sink_cmd_input):
        self.__testbed_cmd_input = testbed_cmd_input
        self.__source_cmd_input = source_cmd_input
        self.__midbox_cmd_input = midbox_cmd_input
        self.__sink_cmd_input = sink_cmd_input

    @property
    def testbed_cmd_input(self):
        return self.__testbed_cmd_input

    @property
    def source_cmd_input(self):
        return self.__source_cmd_input

    @property
    def midbox_cmd_input(self):
        return self.__midbox_cmd_input

    @property
    def sink_cmd_input(self):
        return self.__sink_cmd_input

class LinuxTestBedCtrler(TestBedCtrler):
    def __init__(self, source_namespacec = "ns_snd", midbox_namespace = "ns_mid", sink_namespace = "ns_rcv"):
        self.__shell = ShellCommander()
        self.__source_commander = LinuxNamespaceCommander(source_namespacec)
        source_cmd_input = Sink(self.__source_commander.execute)

        self.__midbox_commander = LinuxNamespaceCommander(midbox_namespace)
        midbox_cmd_input = Sink(self.__midbox_commander.execute)

        self.__sink_commander = LinuxNamespaceCommander(sink_namespace)
        sink_cmd_input = Sink(self.__sink_commander.execute)
        TestBedCtrler.__init__(self, None, source_cmd_input, midbox_cmd_input, sink_cmd_input)
