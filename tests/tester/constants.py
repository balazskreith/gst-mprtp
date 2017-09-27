from enum import Enum

class Algorithms(Enum):
    """
    Represent the available algorithms supported by the tester
    """
    FRACTaL = 1
    SCReAM = 2

class QueueAlgorithms(Enum):
    """
    Represent the available queueing algorithms supported by the tester
    """
    DROP_TAIL = 1

class Codecs(Enum):
    """
    Represent the available codecs supported by the tester
    """
    VP8 = 1

class CommandInterfaces(Enum):
    """
    Represent the available command interfaces supported by the tester
    """
    Shell = 1
