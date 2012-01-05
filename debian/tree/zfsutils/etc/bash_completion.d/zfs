# Copyright (c) 2010, Aneurin Price <aneurin.price@gmail.com>

# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use,
# copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following
# conditions:

# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
# OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

__zfs_get_commands()
{
    zfs 2>&1 | awk '/^\t[a-z]/ {print $1}' | uniq
}

__zfs_get_properties()
{
    zfs get 2>&1 | awk '$2 == "YES" || $2 == "NO" {print $1}'; echo all
}

__zfs_get_editable_properties()
{
    zfs get 2>&1 | awk '$2 == "YES" {printf("%s=\n", $1)}'
}

__zfs_get_inheritable_properties()
{
    zfs get 2>&1 | awk '$3 == "YES" {print $1}'
}

__zfs_list_datasets()
{
    zfs list -H -o name
}

__zfs_list_filesystems()
{
    zfs list -H -o name -t filesystem
}

__zfs_list_snapshots()
{
    zfs list -H -o name -t snapshot
}

__zfs_list_volumes()
{
    zfs list -H -o name -t volume
}

__zfs_argument_chosen()
{
    for word in $(seq $((COMP_CWORD-1)) -1 2)
    do
        local prev="${COMP_WORDS[$word]}"
        for property in $@
        do
            if [ "x$prev" = "x$property" ]
            then
                return 0
            fi
        done
    done
    return 1
}

__zfs_complete_ordered_arguments()
{
    local list1=$1
    local list2=$2
    local cur=$3
    local extra=$4
    if __zfs_argument_chosen $list1
    then
        COMPREPLY=($(compgen -W "$list2 $extra" -- "$cur"))
    else
        COMPREPLY=($(compgen -W "$list1 $extra" -- "$cur"))
    fi
}

__zfs_complete()
{
    local cur prev cmd cmds
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    cmd="${COMP_WORDS[1]}"
    cmds=$(__zfs_get_commands)

    if [ "${prev##*/}" = "zfs" ]
    then
        COMPREPLY=($(compgen -W "$cmds -?" -- "$cur"))
        return 0
    fi

    case "${cmd}" in
        clone)
            __zfs_complete_ordered_arguments "$(__zfs_list_snapshots)" "$(__zfs_list_filesystems) $(__zfs_list_volumes)" $cur
            return 0
            ;;
        get)
            __zfs_complete_ordered_arguments "$(__zfs_get_properties)" "$(__zfs_list_datasets)" "$cur" "-H -r -p"
            return 0
            ;;
        inherit)
            __zfs_complete_ordered_arguments "$(__zfs_get_inheritable_properties)" "$(__zfs_list_datasets)" $cur
            return 0
            ;;
        list)
            if [ "x$prev" = "x-o" ]
            then
                COMPREPLY=($(compgen -W "$(__zfs_get_properties)" -- "${cur##*,}"))
                local existing_opts=$(expr "$cur" : '\(.*,\)')
                if [ ! "x$existing_opts" = "x" ]
                then
                    COMPREPLY=( "${COMPREPLY[@]/#/${existing_opts}}" )
                fi
            else
                COMPREPLY=($(compgen -W "$(__zfs_list_datasets) -H -r -o" -- "$cur"))
            fi
            return 0
            ;;
        promote)
            COMPREPLY=($(compgen -W "$(__zfs_list_filesystems)" -- "$cur"))
            return 0
            ;;
        rollback|send)
            COMPREPLY=($(compgen -W "$(__zfs_list_snapshots)" -- "$cur"))
            return 0
            ;;
        snapshot)
            COMPREPLY=($(compgen -W "$(__zfs_list_filesystems) $(__zfs_list_volumes)" -- "$cur"))
            return 0
            ;;
        set)
            __zfs_complete_ordered_arguments "$(__zfs_get_editable_properties)" "$(__zfs_list_filesystems) $(__zfs_list_volumes)" $cur
            return 0
            ;;
        *)
            COMPREPLY=($(compgen -W "$(__zfs_list_datasets)" -- "$cur"))
            return 0
            ;;
    esac

}

__zpool_get_commands()
{
    zpool 2>&1 | awk '/^\t[a-z]/ {print $1}' | uniq
}

__zpool_get_properties()
{
    zpool get 2>&1 | awk '$2 == "YES" || $2 == "NO" {print $1}'; echo all
}

__zpool_get_editable_properties()
{
    zpool get 2>&1 | awk '$2 == "YES" {printf("%s=\n", $1)}'
}

__zpool_list_pools()
{
    zpool list -H -o name
}

__zpool_complete()
{
    local cur prev cmd cmds
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    cmd="${COMP_WORDS[1]}"
    cmds=$(__zpool_get_commands)

    if [ "${prev##*/}" = "zpool" ]
    then
        COMPREPLY=($(compgen -W "$cmds" -- "$cur"))
        return 0
    fi

    case "${cmd}" in
        get)
            __zfs_complete_ordered_arguments "$(__zpool_get_properties)" "$(__zpool_list_pools)" $cur
            return 0
            ;;
        import)
            if [ "x$prev" = "x-d" ]
            then
                _filedir -d
            else
                COMPREPLY=($(compgen -W "$(__zpool_list_pools) -d" -- "$cur"))
            fi
            return 0
            ;;
        set)
            __zfs_complete_ordered_arguments "$(__zpool_get_editable_properties)" "$(__zpool_list_pools)" $cur
            return 0
            ;;
        add|attach|clear|create|detach|offline|online|remove|replace)
            local pools="$(__zpool_list_pools)"
            if __zfs_argument_chosen $pools
            then
                _filedir
            else
                COMPREPLY=($(compgen -W "$pools" -- "$cur"))
            fi
            return 0
            ;;
        *)
            COMPREPLY=($(compgen -W "$(__zpool_list_pools)" -- "$cur"))
            return 0
            ;;
    esac

}

complete -F __zfs_complete zfs
complete -o filenames -F __zpool_complete zpool
