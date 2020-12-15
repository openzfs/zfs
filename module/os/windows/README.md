
[![Build status](https://ci.appveyor.com/api/projects/status/dcw734sl0prmolwr/branch/master?svg=true)](https://ci.appveyor.com/project/lundman/openzfs/branch/master)


# To setup a development environment for compiling ZFS.


Download free development Windows 10 image from Microsoft.

https://developer.microsoft.com/en-us/windows/downloads/virtual-machines

and create two VMs.

* Host (running Visual Studio and Kernel Debugger)
* Target (runs the compiled kernel module)

The VM images comes with Visual Studio 2017, which we use to compile the driver.

It is recommended that the VMs are placed on static IP, as they can
change IP with all the crashes, and you have to configure the remote
kernel development again.

Go download the Windows Driver Kit 10

https://developer.microsoft.com/en-us/windows/hardware/windows-driver-kit

and install on both VMs. You will need both the SDK and WDK:
Download the SDK with the Visual Studio 2017 community edition first and install it.
It will update the already installed Visual Studio.
Then install the WDK. At the end of the installer, allow it to install the Visual Studio extension.


On Target VM, complete the guide specified here, under
section "Prepare the target computer for provisioning".

https://msdn.microsoft.com/windows/hardware/drivers/gettingstarted/provision-a-target-computer-wdk-8-1?f=255&MSPPError=-2147217396

Which mostly entails running:

C:\Program Files (x86)\Windows Kits\10\Remote\x64\WDK Test Target Setup x64-x64_en-us.msi

* reboot Target VM


On the Host VM, continue the guide to configure Visual Studio.

* Load Visual Studio, there is no need to load the project yet.
* Menu > Driver > Test > Configure Devices
* Click "Add New Device"
* In "Display name:" enter "Target"
* In "Device type:" leave as "Computer"
* In "Network host name:" enter IP of Target VM, for me "172.16.248.103"
* Provisioning options: o Provision device and choose debugger settings.
* Click "Next >"

It now confirms that it talked to the Target, and note here that
"Host IP" it that of the Host VM, for me, "172.16.248.102", and not to
be confused by the Target IP entered on previous screen.

* Click "Next >"

Watch and wait as remote items are installed on the Target VM. It
will most likely reboot the Target VM as well.

I've had dialog boxes pop up and I agree to installation, but I am not
sure they are supposed to. They probably shouldn't, it would seem it
failed to put WDKRemoteUser in Administrators group. If that happens,
use "lusrmgr.msc" to correct it.

The task "Creating system restore point" will most likely fail and
that is acceptable, however, if other tasks fail, you may need to
retry until they work.

At the end of the run, the output window offers a link to the full
log, which is worth reading if you encounter issues.

When things fail, I start a CMD prompt as Administrator, and paste in
the commands that fail, from the log file. It would be nice if this
process just worked though.

If your version of .NET newer, just move along.

The Target VM should reboot, and login as "WDKRemoteUser".

It is recommended you get GIT bash for Windows and install:

https://git-scm.com/downloads

---

Handling configuration errors with Visual Studio 2019 & WDK 10:

There are some issues with Visual Studio 2019 which can cause the following problem in setting up kernel debugging. 
ERROR: Task “Configuring kernel debugger settings (possible reboot)” failed to complete successfully. Look at the logs in the driver test group explorer for more details on the failure.

This problem is related to MSVC debug tool location mismatch, and as a workaround use the following steps to mitigate this problem:

As Administrator, run Developer Command Prompt for VS 2019 in your Host VM
Run the following commands in the VS Developer Command Prompt:

cd /d %VCToolsRedistDir%\debug_nonredist
MKLINK /J x86\Microsoft.VC141.DebugCRT x86\Microsoft.VC142.DebugCRT
MKLINK /J x64\Microsoft.VC141.DebugCRT x64\Microsoft.VC142.DebugCRT

Retry configuration by following guide to configure Visual Studio mentioned above.

---


Host and Target VMs are now configured.

First time you load the project it might default to

Debug : ARM

you probably want to change ARM ==> X64.

---

Currently OpenZFS is compiled using CMake. The easiest
way to do so is with Visual Studio. You will need to
enable "clang" support, as well as Spectre libraries in
the Visual Studio Installer.

Open Visual Studio 2019 (As of Nov 2020)
File -> Open -> Folder

and open the top source folder. Hit build when ready.

It is expected of you to set the environment variables
(either globally, or in your CMakeSettings.json)
		${OPENZFS_SIGNTOOL_CERTSTORE}
		${OPENZFS_SIGNTOOL_CERTNAME}
		${OPENZFS_SIGNTOOL_TSA}

Only the top `driver.c` is compiled using MSVC, and the
linking of OpenZFS.sys.
All other files are compiled using clang, and linked into
libraries.

---

You will need to download OpenSSL for Windows:
Non-Light version recommended for Developers. Install to openssl/bin.
https://slproweb.com/products/Win32OpenSSL.html

---

Deploying with Visual Studio.

If you wish to use Visual Studio to deploy, and debug, against
remote Windows kernel. You can load the
contrib/windows/OpenZFS.sln Solutions file.

This file is only used for deploying!

You are still expected to compile the OpenZFS code using
CMake as described above. This section only compiles
`driver.c` and links against already compiled libraries.


* Configuration Properties > Debugging
"Debugging tools for Windows - Kernel Debugger"
Remote Computer Name: Target

* Configuration Properties > Driver Install > Deployment
Target Device Name: Target
[Tick] Remove previous driver versions
O Hardware ID Driver Update
Root\OpenZFS


You can run DbgView on the Target VM to see the kernel prints on that VM.


Run the compiled Target

* Compile solution
* Menu > Debug > Start Debugging (F5)

wait a while, for Visual Studio to deploy the .sys file on Target and start it.





Target VM optionals.

If you find it frustrating to do development work when Windows Defender or
Windows Updates run, you can disable those in gpedit.msc

* Computer Configuration > Administrative Templates >
     Windows Components >
	 Windows Defender
	 Windows Updates


---

# Milestones


  ✅ Compile SPL sources
  *  Godzillion warnings yet to be addressed

  ✅ Port SPL sources, atomics, mutex, kmem, condvars
  *  C11 _Atomics in kmem not yet handled

  ✅ Compile ZFS sources, stubbing out code as needed

  ✅ Include kernel zlib library

  ✅ Load and Unload SPL and ZFS code

  ✅ Port kernel `zfs_ioctl.c` to accept ioctls from userland

  ✅ Compile userland libspl, libzpool, libzfs, ...

  ✅ Include pthread wrapper library
  *  Replaced with thin pthread.h file

  ✅ Include userland zlib library

  ✅ Compile cmd/zpool

  ✅ Port functions in libzpool, libzfs. Iterate disks, ioctl

  ✅ Test ioctl from zpool to talk to kernel

  ✅ Port kernel `vdev_disk.c` / `vdev_file.c` to issue IO

  ✅ Port over cmd/zfs

  ✅ Add ioctl calls to MOUNT and create Volume to attach

  ✅ Add ioctl calls to UNMOUNT and detach and delete Volume

  ✅ Port kernel `zfs_vnops.c` / `zfs_vnops_windows.c`
  *  Many special cases missing, flags to create/read/etc

  ✅ Correct file information (dates, size, etc)

  ✅ Basic DOS usage

  ✅ Simple Notepad text edit, executables also work.

  ✅ Basic drag'n'drop in Explorer

  ✅ zfs send / recv, file and pipe.

  ✅ ZVOL support

  ✅ git clone ZFS repo on ZFS mounted fs

  ✅ Compile ZFS on top of ZFS

  ❎ Scrooge McDuck style swim in cash

---

# Design issues that need addressing.

* Windows do not handle EFI labels, for now they are parsed with
libefi, and we send offset and size with the filename, that both
libzfs and kernel will parse out and use. This works for a proof
of concept.

Possibly a more proper solution would be to write a thin virtual
hard disk driver, which reads the EFI label and present just the
partitions.

* vdev_disk.c spawns a thread to get around that IoCompletionRoutine
is called in a different context, to sleep until signalled. Is there
a better way to do async in Windows?

* ThreadId should be checked, using PsGetCurrentThreadId() but
it makes zio_taskq_member(taskq_member()) crash. Investigate.

* Functions in posix.c need sustenance.

* The Volume created for MOUNT has something wrong with it, we are
  unable to query it for mountpoint, currently has to string compare a
  list of all mounts. Possibly also related is that we can not call
  any of the functions to set mountpoint to change it. This needs to
  be researched.

* Find a way to get system RAM in SPL, so we can size up the kmem as
expected. Currently looks up the information in the Registry.
kmem should also use Windows signals
"\KernelObjects\LowMemoryCondition" to sense pressure.

Thinking on mount structure. Second design:

Add dataset property WinDriveLetter, which is ignored on Unix system.
So for a simple drive letter dataset:

zfs set driveletter=Z pool

The default creating of a new pool, AND, importing a UNIX pool, would
set the root dataset to

driveletter=?:

So it is assigned first-available drive letter. All lower datasets
will be mounted inside the drive letter. If pool's WinDriveLetter is
not set, it will mount "/pool" as "C:/pool".

---

# Installing a binary release

Latest binary files are available at [GitHub releases](https://github.com/openzfsonwindows/OpenZFS/releases)


If you are running windows 10 with secure boot on and/or installing an older release you will need to enable unsigned drivers from an elevated CMD:

 
* `bcdedit.exe -set testsigning on `
* Then **reboot**. After restart it should have _Test Mode_ bottom right corner of the screen.


After that either 

* Run OpenZFSOnWindows.exe installer to install
* *Would you like to install device software?* should pop up, click install
  * If installing an unsigned release, click "Install anyway" in the "unknown developer" popup

Or if you do not want to run the Installer, run this command by hand from elevated CMD:
* `zfsinstaller.exe install .\OpenZFS.inf`
* *Would you like to install device software?* should pop up, click install
  * If installing an unsigned release, click "Install anyway" in the "unknown developer" popup

Run `zpool.exe status` to confirm it can talk to the kernel

Failure would be:
```
Unable to open \\.\ZFS: No error.
```

Success would be:
```
No pools available
```

---

# Creating your first pool.

The basic syntax to creating a pool is as below. We use the pool name
"tank" here as with Open ZFS documentation. Feel free to pick your own
pool name.

```
# zpool create [options] tank disk
  - Create single disk pool

# zpool create [options] tank mirror disk1 disk2
  - Create mirrored pool ("raid1")

# zpool create [options] tank raidz disk1 disk2 disk3 .... diskn
  - Create raidz ("raid5") pool of multiple disks

```

The default _options_ will "mostly" work in Windows, but for best compatibility
should use a case insensitive filesystem.

The recommended _options_ string for Windows is currently:

```
zpool create -O casesensitivity=insensitive -O compression=lz4 \
     -O atime=off -o ashift=12 tank disk
```

* Creating filebased pools would look like:
```
# fsutil file createnew C:\poolfile.bin 200000000
# zpool.exe create tank \\?\C:\poolfile.bin

Note that "\\?\C:\" needs to be escaped in bash shell, ie
"\\\\?\\C:\\".

        TEST                   ONLINE       0     0     0
        \??\C:\poolfile.bin  ONLINE       0     0     0
```

* Creating a HDD pool

First, locate disk name

```
# wmic diskdrive list brief
VMware, VMware Virtual S SCSI Disk Device  \\.\PHYSICALDRIVE2  VMware, VMware Virtual S SCSI Disk Device  0           5362882560
# zpool create tank PHYSICALDRIVE2
```

# Creating a ZVOL virtual hard disk

Creating a virtual hard disk (ZVOL) is done by passing "-V <size>" to the "zfs create" command.
```
# zfs create -V 2g tank/hello
```

Which would create a disk of 2GB in size, called "tank/hello". 
Confirm it was created with:

```
# wmic diskdrive list brief
Caption                           DeviceID            Model                            Partitions  Size
ZVOL tank/hello SCSI Disk Device  \\.\PHYSICALDRIVE2  ZVOL tank/hello SCSI DiskDevice  0           2105671680
```


# Exporting the pool

If you have finished with ZFS, or want to eject the USB or HDD that the
pool resides on, it must first be _exported_. Similar to "ejecting" a
USB device before unplugging it.

```
# zpool export tank
```

# Importing a pool

If a zpool has been created on a disk partition from a different system make
sure the partition label contains "zfs". Otherwise `zpool import` won't
recognize the pool and will fail with "no pools available to import".

```
# zpool import tank
```

# Uninstalling the driver

If you used the Installer, you can browse to "C:\Program Files (x86)\OpenZFS On Windows"
and run the "uninst000.exe" Uninstaller program.

You can also use "Add Remove Programs" from the Settings menu, and
click on "OpenZFS On Windows-debug version x.xx" and select Uninstall.

If you did not use the Installer, you can manually uninstall it:

```
zfsinstaller uninstall .\OpenZFS.inf
```

To verify that the driver got uninstalled properly you can check "zpool.exe status".

When uninstalled with success, "zpool.exe status" should return:
```
Unable to open \\.\ZFS: No error.
```

If the driver is still there, it would be:
```
No pools available
```

A reboot might be necessary to uninstall it completely.

# Tuning

You can use the [registry](https://openzfsonosx.org/wiki/Windows_Registry) to tune various parameters.  
Also, there is [`kstat`](https://openzfsonosx.org/wiki/Windows_kstat) to dynamically change parameters.

# Nightly builds

There are nightly builds available at [AppVeyor](https://ci.appveyor.com/project/lundman/openzfs/branch/master/artifacts)  
- These builds are currently not signed and therefore require test mode to be enabled.

There also are test builds [available here](https://openzfsonosx.org/wiki/Windows_builds). These are "hotfix" builds for allowing people to test specific fixes before they are ready for a release.
