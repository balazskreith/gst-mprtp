# gst-mprtp

**What is it?**
  
The tester system for gst-mprtp is aimed to 
test congestion control capabilities of a multimedia 
video pipeline system. It includes several subsystem:

**mediapipeline**: is a subsystem used to build 
multimedia pipeline automatically based on parameters.

**statsrelayer**: is an application collecting 
and writing measurement resutls sent by 
gstrtpstatmaker2.

**make_delta_statlogs**: is an application 
generates infromation like sender_bitrate, 
receiver_bitrate, lost packets, discarded packets, 
repaired packets, etc.. based on csv file 
a statsrelayer provides.

**tester**: Is the a test orchestrator. 
It executes different test (rmcat1-7, mprtp1-10) 
copying and evaluating the results automatically.

