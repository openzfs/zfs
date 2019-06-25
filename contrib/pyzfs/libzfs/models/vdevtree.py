#
# Copyright 2019 Hudson River Trading LLC.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

from .._constants import SCAN_STATE, VDEV_AUX_STATE, VDEV_STATE


class VDevStat(object):
    """
    From ```zfs.h```
    Virtual Device statistics
    typedef struct vdev_stat {
        hrtime_t    vs_timestamp;   /* time since vdev load */
        uint64_t    vs_state;       /* vdev state belongs to vdev_state enum */
        uint64_t    vs_aux;         /* see vdev_aux_t   */
        uint64_t    vs_alloc;       /* space allocated  */
        uint64_t    vs_space;       /* total capacity   */
        uint64_t    vs_dspace;      /* deflated capacity    */
        uint64_t    vs_rsize;       /* replaceable dev size */
        uint64_t    vs_esize;       /* expandable dev size */
        uint64_t    vs_ops[6];   /* operation count  */
        uint64_t    vs_bytes[6]; /* bytes read/written   */
        uint64_t    vs_read_errors;     /* read errors      */
        uint64_t    vs_write_errors;    /* write errors     */
        uint64_t    vs_checksum_errors; /* checksum errors  */
        uint64_t    vs_initialize_errors;   /* initializing errors  */
        uint64_t    vs_self_healed;     /* self-healed bytes    */
        uint64_t    vs_scan_removing;   /* removing?    */
        uint64_t    vs_scan_processed;  /* scan processed bytes */
        uint64_t    vs_fragmentation;   /* device fragmentation */
        uint64_t    vs_initialize_bytes_done; /* bytes initialized */
        uint64_t    vs_initialize_bytes_est; /* total bytes to initialize */
        uint64_t    vs_initialize_state;    /* vdev_initialzing_state_t */
        uint64_t    vs_initialize_action_time; /* time_t */
        uint64_t    vs_checkpoint_space;    /* checkpoint-consumed space */
        uint64_t    vs_resilver_deferred;   /* resilver deferred    */
        uint64_t    vs_slow_ios;        /* slow IOs */
        uint64_t    vs_trim_errors;     /* trimming errors  */
        uint64_t    vs_trim_notsup;     /* supported by device */
        uint64_t    vs_trim_bytes_done; /* bytes trimmed */
        uint64_t    vs_trim_bytes_est;  /* total bytes to trim */
        uint64_t    vs_trim_state;      /* vdev_trim_state_t */
        uint64_t    vs_trim_action_time;    /* time_t */
    } vdev_stat_t;

    """

    def __str__(self):
        repr = (
                "<VDevStat "
                "timestamp=%s "
                "state=%s "
                "aux=%s "
                "bytes_allocated=%s "
                "bytes_total=%s "
                "bytes_deflated=%s "
                "read_errors=%s "
                "write_errors=%s "
                "checksum_errors=%s "
                ">"
                % (
                    self.timestamp,
                    VDEV_STATE(self.state),
                    VDEV_AUX_STATE(self.aux),
                    self.bytes_allocated,
                    self.bytes_total,
                    self.bytes_deflated,
                    self.read_errors,
                    self.write_errors,
                    self.checksum_errors,
                )
        )
        return repr

    def __init__(
            self,
            timestamp,
            state,
            aux,
            bytes_allocated,
            bytes_total,
            bytes_deflated,
            rsize,
            esize,
            ops,
            rw_bytes,
            read_errors,
            write_errors,
            checksum_errors,
            initialize_errors,
            self_healed,
            scan_removing,
            scan_processed,
            fragmentation,
    ):
        self.timestamp = timestamp
        self.state = state
        self.aux = aux
        self.bytes_allocated = bytes_allocated
        self.bytes_total = bytes_total
        self.bytes_deflated = bytes_deflated
        self.rsize = rsize
        self.esize = esize
        self.ops = ops
        self.rw_bytes = rw_bytes
        self.read_errors = read_errors
        self.write_errors = write_errors
        self.checksum_errors = checksum_errors
        self.initialize_errors = initialize_errors
        self.self_healed = self_healed
        self.scan_removing = scan_removing
        self.scan_processed = scan_processed
        self.fragmentation = fragmentation

    @staticmethod
    def construct_from_vdev_stats(vdev_stats):
        return VDevStat(
            vdev_stats[0],
            vdev_stats[1],
            vdev_stats[2],
            vdev_stats[3],
            vdev_stats[4],
            vdev_stats[5],
            vdev_stats[6],
            vdev_stats[7],
            vdev_stats[8:14],
            vdev_stats[14:20],
            vdev_stats[20],
            vdev_stats[21],
            vdev_stats[22],
            vdev_stats[23],
            vdev_stats[24],
            vdev_stats[25],
            vdev_stats[26],
            vdev_stats[27],
        )


class PoolScanStat(object):
    """
    From ```zfs.h```
    Pool scan statistics
    typedef struct pool_scan_stat {
        /* values stored on disk */
        uint64_t	pss_func;	/* pool_scan_func_t */
        uint64_t	pss_state;	/* dsl_scan_state_t */
        uint64_t	pss_start_time;	/* scan start time */
        uint64_t	pss_end_time;	/* scan end time */
        uint64_t	pss_to_examine;	/* total bytes to scan */
        uint64_t	pss_examined;	/* total bytes located by scanner */
        uint64_t	pss_to_process; /* total bytes to process */
        uint64_t	pss_processed;	/* total processed bytes */
        uint64_t	pss_errors;	/* scan errors	*/
        /* values not stored on disk */
        uint64_t	pss_pass_exam; /* examined bytes per scan pass */
        uint64_t	pss_pass_start;	/* start time of a scan pass */
        uint64_t	pss_pass_scrub_pause; /* pause time of a scurb pass */
        /* cumulative time scrub spent paused, needed for rate calculation */
        uint64_t	pss_pass_scrub_spent_paused;
        uint64_t	pss_pass_issued; /* issued bytes per scan pass */
        uint64_t	pss_issued;	/* total bytes checked by scanner */
    } pool_scan_stat_t;
    """

    def __str__(self):
        ss = (
                "<PoolScanStats "
                "scan_state=%s "
                "start_time=%s "
                "end_time=%s "
                "bytes_to_examine=%d "
                "bytes_examined=%d "
                "bytes_to_process=%d "
                "bytes_processed=%d "
                "scan_errors=%d "
                ">"
                % (
                    SCAN_STATE(self.scan_state),
                    self.start_time,
                    self.end_time,
                    self.bytes_to_examine,
                    self.bytes_examined,
                    self.bytes_to_process,
                    self.bytes_processed,
                    self.scan_errors,
                )
        )
        return ss

    def __init__(
            self,
            pool_scan_func,
            scan_state,
            start_time,
            end_time,
            bytes_to_examine,
            bytes_examined,
            bytes_to_process,
            bytes_processed,
            scan_errors,
            bytes_examined_per_pass,
            scan_pass_start_time,
            scrub_pass_pause_time,
            total_scrub_pause_time_elapsed,
            bytes_issued_per_scan,
            total_bytes_issued,
    ):
        self.pool_scan_func = pool_scan_func
        self.scan_state = scan_state
        self.start_time = start_time
        self.end_time = end_time
        self.bytes_to_examine = bytes_to_examine
        self.bytes_examined = bytes_examined
        self.bytes_to_process = bytes_to_process
        self.bytes_processed = bytes_processed
        self.scan_errors = scan_errors
        self.bytes_examined_per_pass = bytes_examined_per_pass
        self.scan_pass_start_time = scan_pass_start_time
        self.scrub_pass_pause_time = scrub_pass_pause_time
        self.total_scrub_pause_time_elapsed = total_scrub_pause_time_elapsed
        self.bytes_issued_per_scan = bytes_issued_per_scan
        self.total_bytes_issued = total_bytes_issued

    @staticmethod
    def construct_from_pool_scan_stats(ps_stats):
        return PoolScanStat(
            ps_stats[0],
            ps_stats[1],
            ps_stats[2],
            ps_stats[3],
            ps_stats[4],
            ps_stats[5],
            ps_stats[6],
            ps_stats[7],
            ps_stats[8],
            ps_stats[9],
            ps_stats[10],
            ps_stats[11],
            ps_stats[12],
            ps_stats[13],
            ps_stats[14],
        )


class VDevTree(object):
    """
    We lose a fair bit of information when translating the
    vdev_tree information  given to us by ZFS,
    as this class does not have all the properties that are present
    in the ZFS nvlist;
    instead it just contains the properties we require (for now).
    """

    device_type = ""
    devices = []
    spares = []
    caches = []
    logs = None
    parity = 0
    name = ""
    path = ""
    vdev_stats = None
    pool_scan_stats = None

    def __str__(self):
        repr = (
                "<VDevTree "
                "device_type=%s "
                "name=%s "
                "path=%s "
                "devices=%s "
                "spares=%s "
                "caches=%s "
                "vdev_stats=%s "
                "pool_scan_stats=%s "
                ">"
                % (
                    self.device_type,
                    self.name,
                    self.path,
                    self.devices,
                    self.spares,
                    self.caches,
                    self.vdev_stats,
                    self.pool_scan_stats,
                )
        )
        return repr

    def __init__(self, device_type, name, path, devices, spares, caches):
        self.device_type = device_type
        self.name = name
        self.path = path
        self.devices = devices
        self.spares = spares
        self.caches = caches
        pass

    @staticmethod
    def construct_from_vdev_tree(vdev_tree):
        device_tree = {}
        keys = [("device_type", "type"), ("name", "name"), ("path", "path")]
        for c_key, v_key in keys:
            if v_key in vdev_tree:
                device_tree[c_key] = vdev_tree[v_key]
            else:
                device_tree[c_key] = None
        keys = [
            ("devices", "children"),
            ("spares", "spares"),
            ("caches", "l2cache"),
        ]
        for c_key, v_key in keys:
            if v_key in vdev_tree:
                device_tree[c_key] = [
                    VDevTree.construct_from_vdev_tree(disk)
                    for disk in vdev_tree[v_key]
                ]
            else:
                device_tree[c_key] = []
        device_tree = VDevTree(**device_tree)
        if "vdev_stats" in vdev_tree:
            device_tree.vdev_stats = VDevStat.construct_from_vdev_stats(
                vdev_stats=vdev_tree["vdev_stats"]
            )
        if "scan_stats" in vdev_tree:
            device_tree.pool_scan_stats = \
                PoolScanStat.construct_from_pool_scan_stats(
                    ps_stats=vdev_tree["scan_stats"]
                )
        return device_tree
