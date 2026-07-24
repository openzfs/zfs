# Recovering an “ONLINE but Cannot Import” Pool

If `zpool import` lists a pool as ONLINE but it fails to import, the metadata or spacemap may be corrupted.

## Recovery Procedure

```bash
zpool import -Fn <pool>
zpool import -F <pool>
zpool import -FX <pool>
zpool import -o readonly=on <pool>
