import subprocess

import math

from command import *
import os
import logging
import threading

class Commander:
    """
    Represent a Commander
    """
    def __init__(self, stdout = None, stderr = None):
        self.__stdout = stdout
        self.__stderr = stderr

    @property
    def stdout(self):
        return self.__stdout

    @property
    def stderr(self):
        return self.__stderr

    def execute(self, command, shell = False, input_cmd = None):
        """
        Parameters:
        -----------
        command : Command
            The command must be executed
        shell : boolean
            Indicate whether using shell is obligated or not
        input_cmd
            if shell is used input_cmd is going to be the command given to the shell
        """
        raise NotImplementedError

class ShellCommander(Commander):
    """
    Represent a Commander give command in linux shell
    """
    def __init__(self, stdout = None, stderr = None):
        Commander.__init__(self, stdout, stderr)
        pass

    def execute(self, command, shell = False, input_cmd = None):
        with subprocess.Popen(command.message, stdin=subprocess.PIPE, stdout=subprocess.PIPE, shell = shell) as sub_env:
            out, err = sub_env.communicate(input = input_cmd)
            print(command.stdout)
            if (command.stdout != None):
                command.stdout(out)
            if (command.stderr != None):
                command.stderr(err)

            if (self.stdout != None):
                self.stdout(out)
            if (self.stderr != None):
                self.stderr(err)

class LinuxNamespaceCommander(ShellCommander):
    """
    Represent a Commander give command in a specified namespace inside of a linux shell
    """
    def __init__(self, namespace, stdout = None, stderr = None):
        ShellCommander.__init__(self, stdout, stderr)
        self.__namespace = namespace

    def execute(self, command, shell = False, input_cmd = None):
        logging.debug("execute command in " + self.__namespace + ": " + str(command))
        enter_shell_cmd = ShellCommand(" ".join(["ip", "netns", "exec", self.__namespace, "bash"]),
        stdout = command.stdout, stderr = command.stderr)
        super().execute(command = enter_shell_cmd, shell = True, input_cmd = command.to_bytes())

