class Source:
    """
    Represent a source in push-flow
    """
    def __init__(self):
        self.__sink = None

    def connect(self, target):
        """
        Connect this source to a sink in push-flow
        """
        self.__sink = target

    def transmit(self, value):
        """
        Transmit data to a sink in push-flow
        """
        if (self.__sink == None):
            return
        target = self.__sink.target
        target(value)

class Sink:
    """
    Represent a sink in push-flow
    """
    def __init__(self, target):
        """
        Parameters
        ----------
        target : function
            The target function used as a callee
        """
        self.__target = target

    @property
    def target(self):
        """Gets the target property"""
        return self.__target
