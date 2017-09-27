from midboxctrler import MidboxCtrler
from testbedctrler import TestBedCtrler
from flowsctrler import FlowsCtrler
import asyncio
import threading

class TestCtrler:
    """
    Represent a controller for a test
    """
    def __init__(self, testbed_ctrler, midbox_ctrler, flows_ctrler):
        """
        Parameters:
        -------------
        testbed_ctrler : TesbedCtrler
            A controller for the testbed
        midbox_ctrler : MidboxCtrler
            A controller for the middlebox
        flows_ctrler : FlowsCtrler
            A controller for the flows
        """
        # Construct components
        self.__midbox_ctrler = midbox_ctrler
        self.__flows_ctrler = flows_ctrler
        self.__testbed_ctrler = testbed_ctrler

        # Connect components
        self.__midbox_ctrler.cmd_output.connect(testbed_ctrler.midbox_cmd_input)
        self.__flows_ctrler.sink_cmd_output.connect(testbed_ctrler.sink_cmd_input)
        self.__flows_ctrler.source_cmd_output.connect(testbed_ctrler.source_cmd_input)


    def start(self):
        """
        Start the test
        """
        threads = [
            threading.Thread(target=self.__flows_ctrler.start),
            threading.Thread(target=self.__midbox_ctrler.start)
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
