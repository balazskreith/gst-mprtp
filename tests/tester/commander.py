import subprocess
from command import *

class Commander:
    def execute(self, command, shell = False, input_cmd = None):
        raise NotImplementedError

class ShellCommander(Commander):
    def __init__(self):
        pass

    def execute(self, command, shell = False, input_cmd = None):
        with subprocess.Popen(command.message, stdin=subprocess.PIPE, stdout=subprocess.PIPE, shell = shell) as sub_env:
            out, err = sub_env.communicate(input = input_cmd)
            if (command.stdout != None):
                command.stdout(out)
            if (command.stderr != None):
                command.stderr(err)

class LinuxNamespaceCommander(ShellCommander):
    def __init__(self, namespace):
        ShellCommander.__init__(self)
        self.__namespace = namespace

    def execute(self, command, shell = False, input_cmd = None):
        enter_shell_cmd = ShellCommand(" ".join(["ip", "netns", "exec", self.__namespace, "bash"]),
        stdout = command.stdout, stderr = command.stderr)
        # print(enter_shell_cmd.message)
        super().execute(command = enter_shell_cmd, shell = True, input_cmd = command.to_bytes())
        # ns_env = " ".join(["ip", "netns", "exec", self.__namespace, "bash"])
        # with subprocess.Popen(ns_env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, shell=True) as sub_env:
        #     out, err = sub_env.communicate(command.to_bytes())
        #     if (command.stdout != None):
        #         command.stdout(out)
        #     if (command.stderr != None):
        #         command.stderr(err)
