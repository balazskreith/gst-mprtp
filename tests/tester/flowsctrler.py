from time import sleep
from pushers import *
import logging
import threading

class FlowsCtrler:
    def __init__(self):
        self.__sink_cmd_output = Source()
        self.__source_cmd_output = Source()
        self.__flows = []

    def add_flows(self, *flows):
        self.__flows.append(flows)

    def start(self):
        threads = [threading.Thread(target=self.start_flow(flow)) for flow in self.__flows]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        pass

    def stop(self):
        raise NotImplementedError

    def pause(self):
        raise NotImplementedError

    @property
    def source_cmd_output(self):
        return self.__source_cmd_output

    @property
    def sink_cmd_output(self):
        return self.__sink_cmd_output

    def start_flow(self, flow_tuple):
        flow = flow_tuple[0][0]
        logging.debug("start flow: " + str(flow))
        if (0 < flow.start_delay):
            sleep(flow.start_delay)

        sink_start_cmd = flow.sink_unit.get_start_cmd()
        self.sink_cmd_output.transmit(sink_start_cmd)

        if (0 < flow.sink_to_source_delay):
            sleep(flow.sink_to_source_delay)

        source_start_cmd = flow.source_unit.get_start_cmd()
        self.source_cmd_output.transmit(source_start_cmd)
