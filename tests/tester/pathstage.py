from pathconfig import PathConfig

class PathStage(PathConfig):
    """
    Represent a test stage for tests

    Attributes
    ----------
    duration : int
        The duration of the test stage
    """
    def __init__(self, duration, path_config, next_stage = None):
        """

        Initialize a TestStage object

        Parameters
        ----------
        duration : int
            The duration of the test stage
        """
        self.__duration = duration
        self.__path_config = path_config
        self.__next = next_stage

    @property
    def duration(self):
        """Get actual path config"""
        return self.__duration

    @property
    def path_config(self):
        """Get Next Path Config"""
        return self.__path_config

    @property
    def next(self):
        """Get Next Path Config"""
        return self.__next

    def __str__(self):
        return "Path Stage: duration: " + str(self.__duration) + " " + str(self.__path_config)  
