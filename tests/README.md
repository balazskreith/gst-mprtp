#gst-mprtp/tests

##What is it?
  
A test system is developed for measure and    
evaluate MPRTP[1] and FRACTaL[2].
  
###The Latest Version

Details of the latest version can be found at 
https://github.com/multipath-rtp/gst-mprtp/tests.

###Installation

You need to install gst-mprtp as a gstreamer plugin. 
For other congestion control algorithm (i.e.: SCReAM) 
please install their plugin either.

##System overview

.-----.``   ``.-----.``   ``.-----.  
| Snd |<->| Mid |<->| Rcv |  
'-----'``   ``'-----'``   ``'-----'  

Figure 1. The overall test system model.    
Snd,Mid and Rcv are separated domains  
contains duplex interfaces for communicating  
to each other.

In Figure 1. the overall model of the system is shown.  
Snd, Mid and Rcv domains are namespaces in Ubuntu by running  
the ./scripts/setup\_testbed.sh It creates ns\_snd, ns\_mid and ns\_rcv
respectively and interfaces are created as virtual ethernet interfaces.
  
For setup the test environment locate the terminal to the tests directory and type:  
```
sudo ./scripts/setup_testbed.sh 
``` 
This will create the necessary namespaces, interfaces and set the default 
bandwidth.  

###Sender side
You can enter into the sender side by typing:
```
sudo ip netns ns_snd bash
``` 
By typing ifconfig, you can list the virtual interfaces 
belongs to this namespace. The snd\_pipeline program is used for 
building the pipeline for the sender side.    
  
You can list the options for building a pipeline by typing:      
```
./snd\_pipeline --info
``` 
Generally, for sender you need to setup the following components: Source->Encoder->Sender. 
The are respectively use the --source=[options], --codec=[options], and --sender=[options] arguments, 
however --scheduler and --stat might also wanted to be set for setup a measure- and a congestion controller.

###Receiver Side

You can enter into the receiver side by typing:
```
sudo ip netns ns_rcv bash
``` 
By giving the command ifconfig, you can list the virtual interfaces 
belongs to this namespace. The rcv\_pipeline program is used for 
building the pipeline for the receiver side.    
  
You can list the options for building a pipeline by typing:      
```
./rcv\_pipeline --info
``` 
Generally, for receiver you need to setup the following components: Receiver->Decoder->Sink. 
The are respectively use the --receiver=[options], --codec=[options], and --sink=[options] arguments, 
however --playouter and --stat might also wanted to be set for setup a measure- and a congestion controller.

###Middle Box

You can enter into the middle box by typing:
```
sudo ip netns ns_mid bash
``` 
By giving the command ifconfig, you can list the virtual interfaces 
belongs to this namespace. Middle box contains the interfaces enforces a  
delay and bandwidth limitations. For this purpose netem and tbf are currently used.    

##Running a test

After you setup the testbed, you can run the test by running snd\_pipeline and 
rcv\_pipeline in the appropriate namespaces. You can vary the link between them by applying rules 
on interfaces lying on the middle box.

###RMCAT tests

For Real Time Media Congestion Technique (RMCAT) a test evaluation IETF draft is proposed [3].  
It is recommended to any congestion control wants to work for RTP conversational multimedia communication 
to run these test. You can initiate and run test by typing 
```
./scripts/runs/batch/rmcat[X].sh [ALGORITHM] [OWD]
``` 
, where X is the rmcat test you want to run, ALGORITHM is the congestion controller you want to test, 
OWD is the one-way-delay you want to apply between the sender and the receiver. It enters the linux namespaces, 
setup the pipeline and applying rules. For running these test ntrt[4] must be installed in your system. 

##Contacts

Balázs Kreith, Varun Singh, Jörg Ott
     
##Acknowledgements 
  
Special thanks to Jesus Llorente Santos for writing 
the original test scripts. 

##References

[1] MPRTP refernece  
[2] FRACTaL reference  
[3] rmcat eval test draft  
[4] https://github.com/balazskreith/ntrt  
  
