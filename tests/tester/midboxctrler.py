import asyncio
from time import sleep
from pushers import *

class MidboxCtrler:
    def __init__(self):
        self.__sink_cmd_output = Source()
        self.__source_cmd_output = Source()
        self.__path_stages = []

    def add_path_stage(self, *path_stages):
        self.__path_stages.append(path_stages)

    @property
    def path_stages(self):
        return self.__path_stages

    def start(self):
        raise NotImplementedError

    def stop(self):
        raise NotImplementedError

    def pause(self):
        raise NotImplementedError

    @property
    def cmd_output(self):
        return self.__source_cmd_output

class MidboxShellCtrler(MidboxCtrler):
    def __init__(self):
        MidboxCtrler.__init__(self)
        self.__path_ctrlers = []

    def add_path_ctrlers(self, *path_ctrlers):
        self.__path_ctrlers.append(path_ctrlers)

    async def start(self):
        coroutines = [self.start_path_ctrler(path_ctrler) for path_ctrler in self.__path_ctrlers]
        completed, pending = await asyncio.wait(coroutines)

        # futures = [self.start_path_ctrler(path_ctrler) for path_ctrler in self.__path_ctrlers]
        # loop = asyncio.new_event_loop()
        # loop.run_until_complete(asyncio.wait(futures))
        # loop.close()
        # print(self.__path_ctrlers)
        # start our tasks asynchronously in futures

        # tasks = [asyncio.async(self.start_path_ctrler(path_ctrler)) for path_ctrler in self.__path_ctrlers]
        #
        # # untill all futures are done
        # while not all(task.done() for task in tasks):
        #     # take a short nap
        #     yield from asyncio.sleep(0.01)
        pass

    def stop(self):
        pass

    def pause(self):
        pass

    async def start_path_ctrler(self, path_tuple):
        path_ctrler = path_tuple[0]
        # print(path_ctrler.get_cmd())
        for command in path_ctrler.get_cmd():
            self.cmd_output.transmit(command)
