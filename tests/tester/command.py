import logging

class Command:
    def __init__(self, message, stdout = None, stderr = None):
        self.__message = message
        self.__stdout = stdout if stdout != None else logging.debug
        self.__stderr = stderr if stderr != None else logging.debug

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

    def __str__(self):
        return str(self.__message)

class ShellCommand(Command):
    def __init__(self, message, stdout = None, stderr = None):
        Command.__init__(self, message, stdout, stderr)
        # print(message)

    def to_bytes(self):
        return bytearray(str(self.message), 'utf-8')
