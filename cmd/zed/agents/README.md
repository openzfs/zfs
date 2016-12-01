## Fault Management Logic for ZED ##

The integration of Fault Management Daemon (FMD) logic from illumos
is being deployed in three phases. This logic is encapsulated in
several software modules inside ZED.

### ZED+FM Phase 1 ###

All the phase 1 work is in current Master branch. Phase I work includes:

* Add new paths to the persistent VDEV label for device matching.
* Add a disk monitor for generating _disk-add_ and _disk-change_ events.
* Add support for automated VDEV auto-online, auto-replace and auto-expand.
* Expand the statechange event to include all VDEV state transitions.

### ZED+FM Phase 2 (WIP) ###

The phase 2 work primarily entails the _Diagnosis Engine_ and the
_Retire Agent_ modules. It also includes infrastructure to support a
crude FMD environment to host these modules. For additional
information see the **FMD Components in ZED** and **Implementation
Notes** sections below.

### ZED+FM Phase 3 ###

Future work will add additional functionality and will likely include:

* Add FMD module garbage collection (periodically call `fmd_module_gc()`).
* Add real module property retrieval (currently hard-coded in accessors).
* Additional diagnosis telemetry (like latency outliers and SMART data).
* Export FMD module statistics.
* Zedlet parallel execution and resiliency (add watchdog).

### ZFS Fault Management Overview ###

The primary purpose with ZFS fault management is automated diagnosis
and isolation of VDEV faults. A fault is something we can associate
with an impact (e.g. loss of data redundancy) and a corrective action
(e.g. offline or replace a disk). A typical ZFS fault management stack
is comprised of _error detectors_ (e.g. `zfs_ereport_post()`), a _disk
monitor_, a _diagnosis engine_ and _response agents_.

After detecting a software error, the ZFS kernel module sends error
events to the ZED user daemon which in turn routes the events to its
internal FMA modules based on their event subscriptions. Likewise, if
a disk is added or changed in the system, the disk monitor sends disk
events which are consumed by a response agent.

### FMD Components in ZED ###

There are three FMD modules (aka agents) that are now built into ZED.

  1. A _Diagnosis Engine_ module (`agents/zfs_diagnosis.c`)
  2. A _Retire Agent_ module (`agents/zfs_retire.c`)
  3. A _Disk Add Agent_ module (`agents/zfs_mod.c`)

To begin with, a **Diagnosis Engine** consumes per-vdev I/O and checksum
ereports and feeds them into a Soft Error Rate Discrimination (SERD)
algorithm which will generate a corresponding fault diagnosis when the
tracked VDEV encounters **N** events in a given **T** time window. The
initial N and T values for the SERD algorithm are estimates inherited
from illumos (10 errors in 10 minutes).

In turn, a **Retire Agent** responds to diagnosed faults by isolating
the faulty VDEV. It will notify the ZFS kernel module of the new VDEV
state (degraded or faulted). The retire agent is also responsible for
managing hot spares across all pools. When it encounters a device fault
or a device removal it will replace the device with an appropriate
spare if available.

Finally, a **Disk Add Agent** responds to events from a libudev disk
monitor (`EC_DEV_ADD` or `EC_DEV_STATUS`) and will online, replace or
expand the associated VDEV. This agent is also known as the `zfs_mod`
or Sysevent Loadable Module (SLM) on the illumos platform. The added
disk is matched to a specific VDEV using its device id, physical path
or VDEV GUID.

Note that the _auto-replace_ feature (aka hot plug) is opt-in and you
must set the pool's `autoreplace` property to enable it. The new disk
will be matched to the corresponding leaf VDEV by physical location
and labeled with a GPT partition before replacing the original VDEV
in the pool.

### Implementation Notes ###

* The FMD module API required for logic modules is emulated and implemented
  in the `fmd_api.c` and `fmd_serd.c` source files. This support includes
  module registration, memory allocation, module property accessors, basic
  case management, one-shot timers and SERD engines.
  For detailed information on the FMD module API, see the document --
  _"Fault Management Daemon Programmer's Reference Manual"_.

* The event subscriptions for the modules (located in a module specific
  configuration file on illumos) are currently hard-coded into the ZED
  `zfs_agent_dispatch()` function.

* The FMD modules are called one at a time from a single thread that
  consumes events queued to the modules. These events are sourced from
  the normal ZED events and also include events posted from the diagnosis
  engine and the libudev disk event monitor.

* The FMD code modules have minimal changes and were intentionally left
  as similar as possible to their upstream source files.

* The sysevent namespace in ZED differs from illumos. For example:
    * illumos uses `"resource.sysevent.EC_zfs.ESC_ZFS_vdev_remove"`
    * Linux uses `"sysevent.fs.zfs.vdev_remove"`

* The FMD Modules port was produced by Intel Federal, LLC under award
  number B609815 between the U.S. Department of Energy (DOE) and Intel
  Federal, LLC.

