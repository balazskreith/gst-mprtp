from commander import *
import subprocess
import csv
import gzip

class Evaluator:

    def __clean_target_dir(self):
        if not os.path.isdir(self.__target_dir):
            os.makedirs(self.__target_dir)
        else:
            folder = self.__target_dir
            for the_file in os.listdir(folder):
                file_path = os.path.join(folder, the_file)
                try:
                    if os.path.isfile(file_path):
                        os.unlink(file_path)
                    #elif os.path.isdir(file_path): shutil.rmtree(file_path)
                except Exception as e:
                    print(e)

    """
    Represent an evaluator creating the necessary aggregated statistical files for plotting and summarizing
    """
    @staticmethod
    def __get_flowgroupname(test, flow_name = None):
        parts = [str(test.name)]
        if flow_name:
            parts.append(str(flow_name.replace(' ', '_').lower()))
        parts.append(str(test.algorithm.name).lower())
        parts.append(str(test.latency))
        parts.append(str(test.jitter))

        result = '_'.join(parts)
        return result

    @staticmethod
    def __write_average(source, target):
        average = 0
        with open(source, "r") as f:
            sum = 0
            row_count = 0
            for row in f:
                for column in row.split(','):
                    n=float(column)
                    sum += n
                row_count += 1
            average = sum / row_count
        with open(target, 'w') as f:
            writer = csv.writer(f)
            writer.writerow([average])

    def __init__(self, target_dir = None, save_sources_in_target_dir = True, logsplitter = './logsplitter',
                 statsmaker='./statmaker', clear_target_dir = True):
        self.__target_dir = target_dir if target_dir else './'
        self.__save_sources_in_target_dir = save_sources_in_target_dir
        self.__logsplitter = logsplitter
        self.__statsmaker = statsmaker

        if clear_target_dir:
            self.__clean_target_dir()

    def setup(self, test):
        source_files = self.__prepare(test)
        self.__evaluate_flow_packetlogs(test)

        if self.__save_sources_in_target_dir != './':
            self.__save_sources(source_files)

    def __evaluate_flow_packetlogs(self, test):
        for flow_description in test.get_descriptions():
            flow_id = flow_description.get('flow_id', None)
            evaluations = flow_description.get('evaluations', {})
            sources = flow_description.get('sources', {})
            snd_flowlog = sources.get("snd", None)
            fec_flowlog = sources.get("fec", None)
            rcv_flowlog = sources.get("rcv", None)
            ply_flowlog = sources.get("ply", None)
            tcp_flowlog = sources.get("tcp", None)

            def execute(process_name, *source_files):
                filename = (flow_id + "_" if flow_id else "") + process_name + ".csv"
                evaluations[process_name] = self.__target_dir + filename
                print(' '.join([self.__statsmaker, evaluations[process_name], process_name, ' '.join(source_files)]))
                subprocess.call(' '.join([self.__statsmaker, evaluations[process_name], process_name, ' '.join(source_files)]), shell=True)

            if tcp_flowlog:
                execute("tcpstat", tcp_flowlog)

            if fec_flowlog:
                execute("fec_avg", fec_flowlog)

            if snd_flowlog:
                execute("sr", snd_flowlog)

                if rcv_flowlog:
                    execute("qmd", snd_flowlog, rcv_flowlog)

                if ply_flowlog:
                    execute("lr", snd_flowlog, ply_flowlog)
                    execute("nlf", snd_flowlog, ply_flowlog)

            if ply_flowlog:
                execute("gp_avg", snd_flowlog, ply_flowlog)
                execute("lr", snd_flowlog, ply_flowlog)
                execute("nlf", snd_flowlog, ply_flowlog)

    def __save_sources(self, source_files):
        pcap_files = []
        for logfile in source_files:
            if os.path.exists(logfile) is False:
                continue
            os.rename(logfile, self.__target_dir + logfile)
            if ".pcap" in logfile:
                pcap_files.append(self.__target_dir + logfile)

        for pcap_file in pcap_files:
            content = None
            with open(pcap_file, 'rb') as f:
                content = f.read()
            with gzip.open(pcap_file + '.gz', 'wb') as f:
                f.write(content)
            os.remove(pcap_file)

    def __prepare_flow_packetlogs(self, packetlogs, subflow_id):
        extra_source_files = []
        commands = []
        result = {}
        for packetlog in packetlogs:
            print("Prepare evaluation for " + packetlog)
            source = packetlog
            if subflow_id is not None:
                position = source.index(".csv")
                source = packetlog[:position] + "_s" + str(subflow_id) + packetlog[position:]
                command = [self.__logsplitter, packetlog, source, "subflow_id " + str(subflow_id)]
                subprocess.call(command)
                extra_source_files.append(source)

            if "snd" in source:
                position = source.index("snd_") + 4
                snd_rtp_packets = self.__target_dir + source[:position] + "rtp_" + source[position:]
                commands.append([self.__logsplitter, source, snd_rtp_packets, "payload_type 96"])
                snd_fec_packets = self.__target_dir + source[:position] + "fec_" + source[position:]
                commands.append([self.__logsplitter, source, snd_fec_packets, "payload_type 126"])
                result.update({"snd": snd_rtp_packets})
                result.update({"fec": snd_fec_packets})
            elif "rcv" in source:
                result.update({"rcv": source})
            elif "ply" in source:
                result.update({"ply": source})
            elif "tcp" in source:
                result.update({"tcp": source})
            else:
                print("Unrecognized file:" + source)

        for command in commands:
            subprocess.call(command)

        return result, extra_source_files

    def __prepare(self, test):
        test_description = test.get_descriptions()
        source_files = []
        for flow_description in test_description:
            flow_title = flow_description.get("title", None)
            flow = flow_description.get('flow', None)
            if flow is None:
                print("No Flow for test")
                continue
            source_files.extend(flow.packetlogs)
            source_files.extend(flow.outputlogs)
            subflow_id = flow_description.get("subflow_id", None)
            sources, extra_source_files = self.__prepare_flow_packetlogs(flow.packetlogs, subflow_id)
            source_files.extend(extra_source_files)
            flow_description.update({
                "sources": sources,
                "evaluations": {},
            })

        return source_files
        # for flow_description in test_description:

