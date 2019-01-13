# gst-mprtp

**What is it?**
  
[MPRTP](https://tools.ietf.org/html/draft-singh-avtcore-mprtp-10) 
is the multipath extension to the RTP protocol.
gst-mprtp plugin is a pluggable component to the 
popular Gstreamer application implements 
mprtp protocol. It also contains the implementation 
of the FRACTaL congestion control algorithm, and 
a test environment for evaluating congestion 
control algorithms for both single- and multiple 
path is found in this repository.

**Installation**

gst-mprtp is a gstreamer plugin, hence can be installed
as any individually developed plugin for gstreamer. 
Use  `./autogen.sh && sudo make install` to build the plugin and install it in your system.
You can verify the install by typing 
`gst-inspect-1.0 mprtp`, and see whether the plugins are there.

_Note_: If you use binaries instead of building everything from source, type `export GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0` in order to make mprtp plugins accessible. (and then validate gst-inspect-1.0 mprtp)

**Plugins**

**mprtpscheduler**: A multipath scheduler must be placed 
in the sender side. It is responsible for scheduling the 
packets to subflows based on fixed bitrates assigned 
to subflows or the ones determined by a congestion controller 
a subflow applies.

**mprtpsender**: Responsible for sending packets 
on several attached subflows. It exemines the 
packet goes through it, whether it is an MPRTP 
packet or not and if it is it assigns the packet 
to a subflow it is supposed to be sent. If the 
packet is not an MPRTP packet it sends either 
on a random output or on the default one.

**mprtpreceiver**: Responsible for receiving packets 
from several subflows and forwards all of it 
to the successor component. If the received packet 
is MPRTCP it forwards it on a different output 
than any other packet.

**mprtpplayouter**: A multipath playouter component must be 
placed at the sender side. Responsible for playing out 
packets for the a successor components. It is 
also responsible for monitoring subflows and 
sending reports back the sender side.

**rtpstatmaker2**: A plugin used for collecting 
informations about RTP packets sent through the system.

Details of the latest version can be found at 
https://github.com/multipath-rtp/gst-mprtp.

  
**Tests**

For tests you need tc and you need to run the following scripts:
 
1. Enter to gst-mprtp/tests directory in sudo su mode.

2. Run sudo ./scripts/setup_testbed.sh from tests directory.

3. Run sudo python3 tester/main.py --help

For more information please go to the test directory and read 
the information available in the readme file.

**Contacts**

Balázs Kreith, Varun Singh, Jörg Ott
     
**Acknowledgements** 
  
Special thanks to Jesus Llorente Santos for writing 
the original test scripts. 
  
