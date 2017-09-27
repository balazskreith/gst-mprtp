from constants import *
from flows import *
from testbedctrler import *
from midboxctrler import *
from flowsctrler import *
from pathconfig import *
from pathctrler import *
from testctrler import *
from collections import deque
from tests import *
from testctrlerbuilder import *

import logging
import sys

root = logging.getLogger()
root.setLevel(logging.DEBUG)

ch = logging.StreamHandler(sys.stdout)
ch.setLevel(logging.DEBUG)
formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
ch.setFormatter(formatter)
root.addHandler(ch)

def make_test(type):
    result = None
    if (type == "rmcat1"):
        result = RMCAT1(
          latency=50,
          jitter=0,
          source_type="FILE:foreman_cif.yuv:1:352:288:2:25/1",
          sink_type="FAKESINK"
        )
    return result

test = make_test('rmcat1')
test_ctrler = TestCtrlerBuilder.make(test)
test_ctrler.start()
