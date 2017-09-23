from time import sleep
from pushers import *
import asyncio

class FlowsCtrler:
    def __init__(self):
        self.__sink_cmd_output = Source()
        self.__source_cmd_output = Source()
        self.__flows = []

    def add_flows(self, *flows):
        self.__flows.append(flows)

    async def start(self):
        coroutines = [self.start_flow(flow) for flow in self.__flows]
        completed, pending = await asyncio.wait(coroutines)
        # futures = [self.start_flow(flow) for flow in self.__flows]
        # loop = asyncio.new_event_loop()
        # loop.run_until_complete(asyncio.wait(futures))
        # loop.close()

        #  # start our tasks asynchronously in futures
        # tasks = [asyncio.async(self.start_flow(flow)) for flow in self.__flows]
        #
        # # untill all futures are done
        # while not all(task.done() for task in tasks):
        #     # take a short nap
        #     yield from asyncio.sleep(0.01)
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

    async def start_flow(self, flow_tuple):
        # print(flow[0][0].start_delay)
        flow = flow_tuple[0][0]
        if (0 < flow.start_delay):
            sleep(flow.start_delay)

        sink_start_cmd = flow.source_unit.get_start_cmd()
        self.sink_cmd_output.transmit(sink_start_cmd)

        if (0 < flow.sink_to_source_delay):
            sleep(flow.sink_to_source_delay)

        source_start_cmd = flow.source_unit.get_start_cmd()
        self.source_cmd_output.transmit(source_start_cmd)
