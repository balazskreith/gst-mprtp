from pathstage import PathStage
from command import *
from time import sleep

class PathCtrler:
    """
    Represent an abstract controller for the path

    Attributes
    ----------
    path_name : str
        the ethernet port of the path
    path_stage : PathStage
        The actual path stage
    get_cmd : Command[]
        Returns a sequence of commands for controlling the path
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
        self.__stop = False

    def stop(self):
        """Get stop"""
        self.__stop = True

    def isStopped(self):
        return self.__stop

    @property
    def path_name(self):
        """Get path_name"""
        return self.__path_name

    @property
    def path_stage(self):
        """Get path_name"""
        return self.__path_stage

    def get_cmd(self):
        """Get a sequence of cmd"""
        raise NotImplementedError


class PathShellCtrler(PathCtrler):
    """
    Represent a controller using shell commands for the path

    """
    def __init__(self, path_name, path_stage):
        """
        Init the parameter for the test

        Parameters
        ----------
        path_name : str
            the ethernet port of the path
        """
        PathCtrler.__init__(self, path_name, path_stage)

    def get_cmd(self):
        actual_stage = self.path_stage
        while actual_stage != None:
            path_config = actual_stage.path_config
            command = ShellCommand("tc qdisc change dev " + self.path_name + " " + \
            "parent 1: handle 2: tbf rate " + str(path_config.bandwidth) + "kbit " + \
            "burst " + str(path_config.burst) + "kbit " + \
            "latency " + str(path_config.latency) + "ms " + \
            "minburst " + str(path_config.min_burst) \
            )
            yield ShellCommand(command, stdout = print, stderr = print)
            for step in range(0, actual_stage.duration):
                sleep(1)
                if (self.isStopped()):
                    return
            # if (0 < actual_stage.duration):
                # sleep(actual_stage.duration)
            actual_stage = actual_stage.next

    def __str__(self):
        """Human readable format for PathStage"""
        stages = []
        actual_stage = self.path_stage
        while actual_stage != None:
            stages.append(str(actual_stage))
            actual_stage = actual_stage.next

        return "PathShellCtrler name: " + self.path_name + " stages: " + "\n".join(stages)
