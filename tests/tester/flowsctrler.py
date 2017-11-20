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
        self.__max_sink_to_source_delay = 0

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

    def stop(self):
        for flow_tuple in self.__flows:
            flow = flow_tuple[0]
            if (flow.source_unit is not None):
                source_stop_cmd = flow.source_unit.get_stop_cmd()
                self.source_cmd_output.transmit(source_stop_cmd)

            if (flow.sink_unit is not None):
                sink_stop_cmd = flow.sink_unit.get_stop_cmd()
                self.sink_cmd_output.transmit(sink_stop_cmd)

    @property
    def source_cmd_output(self):
        """Gets the source command output"""
        return self.__source_cmd_output

    @property
    def sink_cmd_output(self):
        """Gets the sink command output"""
        return self.__sink_cmd_output

    @property
    def max_sink_to_source_delay(self):
        """Gets the __sink_to_source_delay"""
        result = 0
        for flow_tuple in self.__flows:
            flow = flow_tuple[0]
            if (result < flow.sink_to_source_delay):
                result = flow.sink_to_source_delay
        return result

    def start_flow(self, flow_tuple):
        """
        Private method used to start a specific flow for a thread
        """
        flow = flow_tuple[0]
        logging.debug("start flow: " + str(flow))
        sink_cmd_output_transmitter = self.sink_cmd_output.transmit
        source_cmd_output_transmitter = self.source_cmd_output.transmit
        sink_to_join = True
        source_to_join = True
        if (flow.flipped):
            source_cmd_output_transmitter = self.sink_cmd_output.transmit
            sink_cmd_output_transmitter = self.source_cmd_output.transmit

        if (0 < flow.start_delay):
            sleep(flow.start_delay)

        if (flow.sink_unit is not None):
            sink_start_cmd = flow.sink_unit.get_start_cmd()
            sink_thread = threading.Thread(target=sink_cmd_output_transmitter, args=(sink_start_cmd, ))
            sink_thread.start()
        else:
            sink_to_join = False

        if (0 < flow.sink_to_source_delay):
            sleep(flow.sink_to_source_delay)

        if (flow.source_unit is not None):
            source_start_cmd = flow.source_unit.get_start_cmd()
            source_thread = threading.Thread(target=source_cmd_output_transmitter, args=(source_start_cmd, ))
            source_thread.start()
        else:
            source_to_join = False

        if (self.__max_sink_to_source_delay < flow.sink_to_source_delay):
            self.__max_sink_to_source_delay = flow.sink_to_source_delay


        self.__ready = True

        if sink_to_join:
            sink_thread.join()

        if source_to_join:
            source_thread.join()
