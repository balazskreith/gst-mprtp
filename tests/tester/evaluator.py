from commander import *
from command import *
import subprocess
import csv

class Evaluator:
    """
    Represent an evaluator creating the necessary aggregated statistical files for plotting and summarizing
    """
    def __init__(self, target_dir = None, snd_path = None, rcv_path = None, ply_path = None, tcp_path = None, bandwidths = None):
        """
        Parameters:
        -----------
        target_dir : string
            The directoryfor the generated files
        snd_path : string
            The path for the sender logfile
        rcv_path : strging
            The path for the receiver logfile
        ply_path : string
            The path for the playouter logfile
        tcp_path : string
            The path for the tcp logfile
        """
        self.__target_dir = target_dir
        self.__snd_path = snd_path
        self.__rcv_path = rcv_path
        self.__ply_path = ply_path
        self.__tcp_path = tcp_path
        self.__bandwidths = bandwidths
        self.__commander = ShellCommander(stdout = print, stderr = print)
        print(' '.join([snd_path, rcv_path, ply_path]))

    def __str__(self):
        return

    @property
    def cmd_output(self):
        """Get the sink_program_name property"""
        return self.__cmd_output

    def write_bandwidths(self, target):
        with open(target, 'w') as f:
            writer = csv.writer(f)
            for val in self.__bandwidths:
                writer.writerow([val])

    def write_average(self, source, target):
        average = 0
        with open(source, "r") as f:
            Sum = 0
            row_count = 0
            for row in f:
                for column in row.split(','):
                    n=float(column)
                    Sum += n
                row_count += 1
            average = Sum / row_count
        with open(target, 'w') as f:
            writer = csv.writer(f)
            writer.writerow([average])

    def perform(self):
        logsplitter = './logsplitter'
        commands = []
        subindex = self.__snd_path[-6:]
        subindex = subindex[:2]
        result = {
            "sr_csv": self.__target_dir + "sr" + subindex + ".csv",
            "qmd_csv": self.__target_dir + "qmd" + subindex + ".csv",
            "tcp_csv": self.__target_dir + "tcprate" + subindex + ".csv",
            "gp_avg": self.__target_dir + "gp_avg" + subindex + ".csv",
            "fec_avg": self.__target_dir + "fec_avg" + subindex + ".csv",
            "pathbw_csv": self.__target_dir + "pathbw" + subindex + ".csv",
            "lr": self.__target_dir + "lr" + subindex + ".csv",
            "nlf": self.__target_dir + "nlf" + subindex + ".csv",
            "ffre": self.__target_dir + "ffre" + subindex + ".csv",
            "qmd_avg": self.__target_dir + "qmd_avg" + subindex + ".csv",
        }

        position = self.__snd_path.index("snd_") + 4
        snd_rtp_packets = self.__snd_path[:position] + "rtp_" + self.__snd_path[position:]

        # commands.append(["./set","-x"])
        commands.append([logsplitter, self.__snd_path, snd_rtp_packets, "payload_type 96"])

        snd_fec_packets = self.__snd_path[:position] + "fec_" + self.__snd_path[position:]
        commands.append([logsplitter, self.__snd_path, snd_fec_packets, "payload_type 126"])

        self.write_bandwidths(result["pathbw_csv"])

        statsmaker = './statmaker'

        commands.append([statsmaker, result["sr_csv"], "sr", self.__snd_path])
        commands.append([statsmaker, result["qmd_csv"], "qmd", self.__snd_path, self.__rcv_path])

        commands.append([statsmaker, result["gp_avg"], "gp_avg", self.__ply_path])
        commands.append([statsmaker, result["fec_avg"], "fec_avg", snd_fec_packets])
        commands.append([statsmaker, result["lr"], "lr", snd_rtp_packets, self.__ply_path])
        commands.append([statsmaker, result["nlf"], "nlf", snd_rtp_packets, self.__ply_path])

        if (self.__tcp_path is not None):
            commands.append([statsmaker, result["tcp_csv"], "tcpstat", self.__tcp_path])
        else:
            result["tcp_csv"] = None
        # commands.append([statsmaker, result["ffre"], "ffre", snd_fec_packets, snd_rtp_packets, self.__rcv_path, self.__ply_path])

        # commands.append(["./paste.sh"])
        # commands.append(["exec", "paste", self.__target_dir.join(["", "qmd.csv ", "sr.csv ", "pathbw.csv "]), " > " + self.__target_dir + "plotstats.csv"])
        # print(" ".join(["paste", self.__target_dir.join(["", "qmd.csv ", "sr.csv ", "pathbw.csv "]), " > " + self.__target_dir + "plotstats.csv"]))
        # commands.append(["awk", '\'{sum+=$1; ++n} END { print sum/(n) }\' ' + self.__target_dir + "qmd.csv > " + self.__target_dir + "qmd_avg.csv"])
        for message in commands:
            subprocess.call(message)
            # command = ShellCommand(message)
            # self.__commander.execute(command = command)

        self.write_average(result["qmd_csv"], result["qmd_avg"])
        return result


class MPEvaluator:
    """
    Represent an evaluator creating the necessary aggregated statistical files for plotting and summarizing
    """
    def __init__(self, target_dir = None, snd_path = None, rcv_path = None, ply_path = None, tcp_path = None, bandwidths = None, multipath_flownum = 1):
        """
        Parameters:
        -----------
        target_dir : string
            The directoryfor the generated files
        snd_path : string
            The path for the sender logfile
        rcv_path : strging
            The path for the receiver logfile
        ply_path : string
            The path for the playouter logfile
        tcp_path : string
            The path for the tcp logfile
        """
        self.__target_dir = target_dir
        self.__snd_path = snd_path
        self.__rcv_path = rcv_path
        self.__ply_path = ply_path
        self.__tcp_path = tcp_path
        self.__bandwidths = bandwidths
        self.__multipath_flownum = multipath_flownum
        self.__commander = ShellCommander(stdout = print, stderr = print)
        print(' '.join([snd_path, rcv_path, ply_path]))

    def __str__(self):
        return

    @property
    def cmd_output(self):
        """Get the sink_program_name property"""
        return self.__cmd_output

    def write_bandwidths(self, target):
        with open(target, 'w') as f:
            writer = csv.writer(f)
            for val in self.__bandwidths:
                writer.writerow([val])

    def write_average(self, source, target):
        average = 0
        with open(source, "r") as f:
            Sum = 0
            row_count = 0
            for row in f:
                for column in row.split(','):
                    n=float(column)
                    Sum += n
                row_count += 1
            average = Sum / row_count
        with open(target, 'w') as f:
            writer = csv.writer(f)
            writer.writerow([average])

    def perform(self):
        logsplitter = './logsplitter'
        commands = []
        subindex = self.__snd_path[-6:]
        subindex = subindex[:2]

        for subflow_id in range(1, self.__multipath_flownum + 1):

            if 0 < self.__multipath_flownum:
                subflow_postfix = "_" + str(subflow_id)
            else:
                subflow_postfix = ""

        result = {
            "sr_csv": self.__target_dir + "sr" + subindex + ".csv",
            "qmd_csv": self.__target_dir + "qmd" + subindex + ".csv",
            "tcp_csv": self.__target_dir + "tcprate" + subindex + ".csv",
            "gp_avg": self.__target_dir + "gp_avg" + subindex + ".csv",
            "fec_avg": self.__target_dir + "fec_avg" + subindex + ".csv",
            "pathbw_csv": self.__target_dir + "pathbw" + subindex + ".csv",
            "lr": self.__target_dir + "lr" + subindex + ".csv",
            "nlf": self.__target_dir + "nlf" + subindex + ".csv",
            "ffre": self.__target_dir + "ffre" + subindex + ".csv",
            "qmd_avg": self.__target_dir + "qmd_avg" + subindex + ".csv",
        }

        position = self.__snd_path.index("snd_") + 4
        snd_rtp_packets = self.__snd_path[:position] + "rtp_" + self.__snd_path[position:]

        # commands.append(["./set","-x"])
        commands.append([logsplitter, self.__snd_path, snd_rtp_packets, "payload_type 96"])

        snd_fec_packets = self.__snd_path[:position] + "fec_" + self.__snd_path[position:]
        commands.append([logsplitter, self.__snd_path, snd_fec_packets, "payload_type 126"])

        self.write_bandwidths(result["pathbw_csv"])

        statsmaker = './statmaker'

        commands.append([statsmaker, result["sr_csv"], "sr", self.__snd_path])
        commands.append([statsmaker, result["qmd_csv"], "qmd", self.__snd_path, self.__rcv_path])

        commands.append([statsmaker, result["gp_avg"], "gp_avg", self.__ply_path])
        commands.append([statsmaker, result["fec_avg"], "fec_avg", snd_fec_packets])
        commands.append([statsmaker, result["lr"], "lr", snd_rtp_packets, self.__ply_path])
        commands.append([statsmaker, result["nlf"], "nlf", snd_rtp_packets, self.__ply_path])

        if (self.__tcp_path is not None):
            commands.append([statsmaker, result["tcp_csv"], "tcpstat", self.__tcp_path])
        else:
            result["tcp_csv"] = None
        # commands.append([statsmaker, result["ffre"], "ffre", snd_fec_packets, snd_rtp_packets, self.__rcv_path, self.__ply_path])

        # commands.append(["./paste.sh"])
        # commands.append(["exec", "paste", self.__target_dir.join(["", "qmd.csv ", "sr.csv ", "pathbw.csv "]), " > " + self.__target_dir + "plotstats.csv"])
        # print(" ".join(["paste", self.__target_dir.join(["", "qmd.csv ", "sr.csv ", "pathbw.csv "]), " > " + self.__target_dir + "plotstats.csv"]))
        # commands.append(["awk", '\'{sum+=$1; ++n} END { print sum/(n) }\' ' + self.__target_dir + "qmd.csv > " + self.__target_dir + "qmd_avg.csv"])

        for message in commands:
            subprocess.call(message)
            # command = ShellCommand(message)
            # self.__commander.execute(command = command)

        self.write_average(result["qmd_csv"], result["qmd_avg"])
        return result
