from testbedctrler import *
from midboxctrler import *
from flowsctrler import *
from pathconfig import *
from pathctrler import *
from testctrler import *

class TestCtrlerBuilder(object):
    """Represent a builder for testcontrollers"""
    def __init__(self):
        super(TestCtrlerBuilder, self).__init__()

    @staticmethod
    def make(test):
        """
        Make a testctrler
        """
        tesbed_ctrler = LinuxTestBedCtrler()
        midbox_ctrler = MidboxShellCtrler()
        flows_ctrler = FlowsCtrler()

        [midbox_ctrler.add_path_ctrlers(path_ctrler) for path_ctrler in test.get_path_ctrlers()]
        [flows_ctrler.add_flows(flow) for flow in test.get_flows()]

        result = TestCtrler(tesbed_ctrler, midbox_ctrler, flows_ctrler)
        return result;
