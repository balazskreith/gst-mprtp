from time import sleep
from pushers import *
import logging
import threading

class MidboxCtrler:
    def __init__(self):
        """
        Represent a Controller for the middlebox
        """
        self.__sink_cmd_output = Source()
        self.__source_cmd_output = Source()

    def start(self):
        """Start the controller"""
        raise NotImplementedError

    def stop(self):
        """Stop the controller"""
        raise NotImplementedError

    @property
    def cmd_output(self):
        """Get the command output property"""
        return self.__source_cmd_output

class MidboxShellCtrler(MidboxCtrler):
    def __init__(self):
        MidboxCtrler.__init__(self)
        self.__path_ctrlers = []

    def add_path_ctrlers(self, *path_ctrlers):
        self.__path_ctrlers.append(path_ctrlers)

    def start(self):
        threads = [threading.Thread(target=self.start_path_ctrler, args=(path_ctrler, )) for path_ctrler in self.__path_ctrlers]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()

    def stop(self):
        for path_tuple in self.__path_ctrlers:
            path_ctrler = path_tuple[0]
            path_ctrler.stop()

    def start_path_ctrler(self, path_tuple):
        path_ctrler = path_tuple[0]
        logging.debug("start path ctrler: " + str(path_ctrler))
        for command in path_ctrler.get_cmd():
            logging.debug(command)
            self.cmd_output.transmit(command)
