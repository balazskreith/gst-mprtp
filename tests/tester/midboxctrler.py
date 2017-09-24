from time import sleep
from pushers import *
import logging
import threading

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

    def start(self):
        threads = [threading.Thread(target=self.start_path_ctrler(path_ctrler)) for path_ctrler in self.__path_ctrlers]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        pass

    def stop(self):
        pass

    def pause(self):
        pass

    def start_path_ctrler(self, path_tuple):
        path_ctrler = path_tuple[0]
        logging.debug("start path ctrler: " + str(path_ctrler))
        for command in path_ctrler.get_cmd():
            logging.debug(command)
            self.cmd_output.transmit(command)
