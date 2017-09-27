from time import sleep
from pushers import *
import logging
import threading

class FlowsCtrler:
    """
    Represent controller for flow objects
    """
    def __init__(self):
        self.__sink_cmd_output = Source()
        self.__source_cmd_output = Source()
        self.__flows = []

    def add_flows(self, *flows):
        """
        Parameters:
        -----------
        flows : [Flow]
            A list of flows separated by a comma and added to the controller
        """
        self.__flows.append(flows)

    def start(self):
        """
        Start the controller
        """
        threads = [threading.Thread(target=self.start_flow, args=(flow, )) for flow in self.__flows]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        pass

    @property
    def source_cmd_output(self):
        """Gets the source command output"""
        return self.__source_cmd_output

    @property
    def sink_cmd_output(self):
        """Gets the sink command output"""
        return self.__sink_cmd_output

    def start_flow(self, flow_tuple):
        """
        Private method used to start a specific flow for a thread
        """
        flow = flow_tuple[0]
        logging.debug("start flow: " + str(flow))
        if (0 < flow.start_delay):
            sleep(flow.start_delay)

        sink_start_cmd = flow.sink_unit.get_start_cmd()
        sink_thread = threading.Thread(target=self.sink_cmd_output.transmit, args=(sink_start_cmd, ))
        sink_thread.start()
        if (0 < flow.sink_to_source_delay):
            sleep(flow.sink_to_source_delay)

        source_start_cmd = flow.source_unit.get_start_cmd()
        source_thread = threading.Thread(target=self.source_cmd_output.transmit, args=(source_start_cmd, ))
        source_thread.start()

        sink_thread.join()
        source_thread.join()
