from midboxctrler import MidboxCtrler
from testbedctrler import TestBedCtrler
from flowsctrler import FlowsCtrler
import asyncio
import threading
from time import sleep

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
        flows_ctrler_thread = threading.Thread(target=self.__flows_ctrler.start)
        midbox_ctrler_thread = threading.Thread(target=self.__midbox_ctrler.start)
        flows_ctrler_thread.start()
        sleep(self.__flows_ctrler.max_sink_to_source_delay)
        midbox_ctrler_thread.start()

        flows_ctrler_thread.join()
        midbox_ctrler_thread.join()

    def get_max_source_to_sink_delay(self):
        return self.__flows_ctrler.max_sink_to_source_delay

    def stop(self):
        """
        Stop the test
        """
        self.__flows_ctrler.stop()
        self.__midbox_ctrler.stop()
