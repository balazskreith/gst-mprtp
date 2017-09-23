
class Command:
    def __init__(self, message, stdout = None, stderr = None):
        self.__message = message
        self.__stdout = stdout
        self.__stderr = stderr

    @property
    def message(self):
        return self.__message

    @property
    def stdout(self):
        return self.__stdout

    @property
    def stderr(self):
        return self.__stderr

    def to_bytes(self):
        raise NotImplementedError

class ShellCommand(Command):
    def __init__(self, message, stdout = None, stderr = None):
        Command.__init__(self, message, stdout, stderr)
        # print(message)

    def to_bytes(self):
        return bytearray(self.message, 'utf-8')
