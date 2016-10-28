#!/bin/bash
sudo apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv 7F0CEB10
echo 'deb http://downloads-distro.mongodb.org/repo/ubuntu-upstart dist 10gen' | sudo tee /etc/apt/sources.list.d/mongodb.list
sudo apt-get update
sudo apt-get install gtk-doc-tools autoconf autopoint libtool make git curl mongodb-org bison flex libglib2.0-dev libtheora-dev libvorbis-dev g++ libpng-dev libflac-dev libspeex-dev yasm nettle-dev libgcrypt-dev libmp3lame-dev python3 python3-dev python-gobject-dev -y
sudo apt-get build-dep gstreamer1.0-plugins-{base,good,bad,ugly}
curl -sL https://deb.nodesource.com/setup | sudo bash -

sudo mkdir /home/gst
sudo mkdir /home/gst/sources
cd /home/gst/sources

# set -x
for TARGET in gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly
do
  git clone http://github.com/gstreamer/$TARGET
  cd $TARGET
  libtoolize
  sudo ./autogen.sh
  sudo make install
done

mkdir /home/gst/dev
