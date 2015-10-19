## Installation (for Debian)

First install the dependencies 

```bash

apt-get install libtool build-essential gawk alien fakeroot linux-headers-$(uname -r) zlib1g-dev uuid-dev libblkid-dev libselinux-dev parted lsscsi wget automake

```

Next install the spl  (on the master branch) : 

```bash

git clone https://github.com/zfsonlinux/spl.git
cd spl
./autogen.sh
./configure
make pkg
dpkg -i *.deb
cd ..

```
Now you can clone the zfs (branch with json implementation)"
```bash
git clone -b json-0.6.5 https://github.com/Alyseo/zfs.git
cd zfs
./autogen.sh
./configure
make pkg
dpkg -i *.deb
```
