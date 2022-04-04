#!/bin/sh
# shellcheck disable=SC2154

# only run this on systemd systems, we handle the decrypt in mount-zfs.sh in the mount hook otherwise
[ -e /bin/systemctl ] || [ -e /usr/bin/systemctl ] || return 0

# shellcheck source=zfs-lib.sh.in
. /lib/dracut-zfs-lib.sh

decode_root_args || return 0

# There is a race between the zpool import and the pre-mount hooks, so we wait for a pool to be imported
while ! systemctl is-active --quiet zfs-import.target; do
    systemctl is-failed --quiet zfs-import-cache.service zfs-import-scan.service && return 1
    sleep 0.1s
done

BOOTFS="$root"
if [ "$BOOTFS" = "zfs:AUTO" ]; then
    BOOTFS="$(zpool get -Ho value bootfs | grep -m1 -vFx -)"
fi

[ "$(zpool get -Ho value feature@encryption "${BOOTFS%%/*}")" = 'active' ] || return 0

_load_key_cb() {
    dataset="$1"

    ENCRYPTIONROOT="$(zfs get -Ho value encryptionroot "${dataset}")"
    [ "${ENCRYPTIONROOT}" = "-" ] && return 0

    [ "$(zfs get -Ho value keystatus "${ENCRYPTIONROOT}")" = "unavailable" ] || return 0

    KEYLOCATION="$(zfs get -Ho value keylocation "${ENCRYPTIONROOT}")"
    case "${KEYLOCATION%%://*}" in
        prompt)
            for _ in 1 2 3; do
                systemd-ask-password --no-tty "Encrypted ZFS password for ${dataset}" | zfs load-key "${ENCRYPTIONROOT}" && break
            done
            ;;
        http*)
            systemctl start network-online.target
            zfs load-key "${ENCRYPTIONROOT}"
            ;;
        file)
            KEYFILE="${KEYLOCATION#file://}"
            [ -r "${KEYFILE}" ] || udevadm settle
            [ -r "${KEYFILE}" ] || {
                info "ZFS: Waiting for key ${KEYFILE} for ${ENCRYPTIONROOT}..."
                for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
                    sleep 0.5s
                    [ -r "${KEYFILE}" ] && break
                done
            }
            [ -r "${KEYFILE}" ] || warn "ZFS: Key ${KEYFILE} for ${ENCRYPTIONROOT} hasn't appeared. Trying anyway."
            zfs load-key "${ENCRYPTIONROOT}"
            ;;
        *)
            zfs load-key "${ENCRYPTIONROOT}"
            ;;
    esac
}

_load_key_cb "$BOOTFS"
for_relevant_root_children "$BOOTFS" _load_key_cb
