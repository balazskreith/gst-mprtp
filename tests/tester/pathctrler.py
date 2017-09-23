from pathstage import PathStage
from command import *
from time import sleep

class PathCtrler:
    """
    Represent a configuration for the path

    Attributes
    ----------
    path_name : str
        the ethernet port of the path
    """
    def __init__(self, path_name, path_stage):
        """
        Init the parameter for the test

        Parameters
        ----------
        path_name : str
            the ethernet port of the path
        """
        self.__path_name = path_name
        self.__path_stage = path_stage

    @property
    def path_name(self):
        """Get path_name"""
        return self.__path_name

    @property
    def path_stage(self):
        """Get path_name"""
        return self.__path_stage

    def get_cmd(self):
        raise NotImplementedError

class PathShellCtrler(PathCtrler):
    def get_cmd(self):
        actual_stage = self.path_stage
        while actual_stage != None:
            path_config = actual_stage.path_config
            command = ShellCommand("tc qdisc change dev " + self.path_name + " " + \
            "parent 1: handle 2: tbf rate " + str(path_config.bandwidth) + "kbit " + \
            "burst" + str(path_config.burst) + "kbit " + \
            "latency " + str(path_config.latency) + "ms " + \
            "minburst " + str(path_config.min_burst) \
            )
            yield ShellCommand(command)
            if (0 < actual_stage.duration):
                sleep(actual_stage.duration)
            actual_stage = actual_stage.next
