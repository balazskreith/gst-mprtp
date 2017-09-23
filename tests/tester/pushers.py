class Source:
    def __init__(self):
        self.__sink = None

    def connect(self, target):
        self.__sink = target

    def transmit(self, value):
        if (self.__sink == None):
            return
        target = self.__sink.target
        target(value)

class Sink:
    def __init__(self, target):
        self.__target = target

    @property
    def target(self):
        return self.__target
