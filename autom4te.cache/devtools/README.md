#Development environment tools

Here is some tools for setup a development environment.

##On a non-Linux OS

Vagrantfile - You can use this if you want to setup a virtualbox for gstreamer plugin development.

Note: It makes sense if your host machine is not Linux.
```
vagrant up
```

After you set this up you can enter and build you stuff by using vagrant ssh.


##On Ubuntu
startup.sh - This script is used to build dependencies and install gstreamer plugins from source
using the latest repo from github/gstreamer. It needs sudo to run. Vagrant runs it automatically as a provision.

```
sudo ./startup.sh
```
