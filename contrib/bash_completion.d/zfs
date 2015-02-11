# Copyright (c) 2013, Aneurin Price <aneurin.price@gmail.com>

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

if [[ -w /dev/zfs ]]; then
    __ZFS_CMD="zfs"
    __ZPOOL_CMD="zpool"
else
    __ZFS_CMD="sudo zfs"
    __ZPOOL_CMD="sudo zpool"
fi

__zfs_get_commands()
{
    $__ZFS_CMD 2>&1 | awk '/^\t[a-z]/ {print $1}' | cut -f1 -d '|' | uniq
}

__zfs_get_properties()
{
    $__ZFS_CMD get 2>&1 | awk '$2 == "YES" || $2 == "NO" {print $1}'; echo all name space
}

__zfs_get_editable_properties()
{
    $__ZFS_CMD get 2>&1 | awk '$2 == "YES" {print $1"="}'
}

__zfs_get_inheritable_properties()
{
    $__ZFS_CMD get 2>&1 | awk '$3 == "YES" {print $1}'
}

__zfs_list_datasets()
{
    $__ZFS_CMD list -H -o name -t filesystem,volume
}

__zfs_list_filesystems()
{
    $__ZFS_CMD list -H -o name -t filesystem
}

__zfs_match_snapshot()
{
    local base_dataset=${cur%@*}
    if [[ $base_dataset != $cur ]]
    then
        $__ZFS_CMD list -H -o name -t snapshot -d 1 $base_dataset
    else
        $__ZFS_CMD list -H -o name -t filesystem,volume | awk '{print $1"@"}'
    fi
}

__zfs_match_explicit_snapshot()
{
    local base_dataset=${cur%@*}
    if [[ $base_dataset != $cur ]]
    then
        $__ZFS_CMD list -H -o name -t snapshot -d 1 $base_dataset
    fi
}

__zfs_match_multiple_snapshots()
{
    local existing_opts=$(expr "$cur" : '\(.*\)[%,]')
    if [[ $existing_opts ]]
    then
        local base_dataset=${cur%@*}
        if [[ $base_dataset != $cur ]]
        then
            local cur=${cur##*,}
            if [[ $cur =~ ^%|%.*% ]]
            then
                # correct range syntax is start%end
                return 1
            fi
            local range_start=$(expr "$cur" : '\(.*%\)')
            $__ZFS_CMD list -H -o name -t snapshot -d 1 $base_dataset | sed 's$.*@$'$range_start'$g'
        fi
    else
        __zfs_match_explicit_snapshot; __zfs_list_datasets
    fi
}

__zfs_list_volumes()
{
    $__ZFS_CMD list -H -o name -t volume
}

__zfs_argument_chosen()
{
    local word property
    for word in $(seq $((COMP_CWORD-1)) -1 2)
    do
        local prev="${COMP_WORDS[$word]}"
        if [[ ${COMP_WORDS[$word-1]} != -[tos] ]]
        then
            if [[ "$prev" == [^,]*,* ]] || [[ "$prev" == *[@:]* ]]
            then
                return 0
            fi
            for property in $@
            do
                if [[ $prev == "$property" ]]
                then
                    return 0
                fi
            done
        fi
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

__zfs_complete_multiple_options()
{
    local options=$1
    local cur=$2

    COMPREPLY=($(compgen -W "$options" -- "${cur##*,}"))
    local existing_opts=$(expr "$cur" : '\(.*,\)')
    if [[ $existing_opts ]] 
    then
        COMPREPLY=( "${COMPREPLY[@]/#/${existing_opts}}" )
    fi
}

__zfs_complete_switch()
{
    local options=$1
    if [[ ${cur:0:1} == - ]]
    then
        COMPREPLY=($(compgen -W "-{$options}" -- "$cur"))
        return 0
    else
        return 1
    fi
}

__zfs_complete()
{
    local cur prev cmd cmds
    COMPREPLY=()
    # Don't split on colon
    _get_comp_words_by_ref -n : -c cur -p prev -w COMP_WORDS -i COMP_CWORD
    cmd="${COMP_WORDS[1]}"

    if [[ ${prev##*/} == zfs ]]
    then
        cmds=$(__zfs_get_commands)
        COMPREPLY=($(compgen -W "$cmds -?" -- "$cur"))
        return 0
    fi

    case "${cmd}" in
        clone)
            case "${prev}" in
                -o)
                    COMPREPLY=($(compgen -W "$(__zfs_get_editable_properties)" -- "$cur"))
                    ;;
                *)
                    if ! __zfs_complete_switch "o,p"
                    then
                        if __zfs_argument_chosen
                        then
                            COMPREPLY=($(compgen -W "$(__zfs_list_datasets)" -- "$cur"))
                        else
                            COMPREPLY=($(compgen -W "$(__zfs_match_snapshot)" -- "$cur"))
                        fi
                    fi
                    ;;
            esac
            ;;
        get)
            case "${prev}" in
                -d)
                    COMPREPLY=($(compgen -W "" -- "$cur"))
                    ;;
                -t)
                    __zfs_complete_multiple_options "filesystem volume snapshot all" "$cur"
                    ;;
                -s)
                    __zfs_complete_multiple_options "local default inherited temporary none" "$cur"
                    ;;
                -o)
                    __zfs_complete_multiple_options "name property value source received all" "$cur"
                    ;;
                *)
                    if ! __zfs_complete_switch "H,r,p,d,o,t,s"
                    then
                        if __zfs_argument_chosen $(__zfs_get_properties)
                        then
                            COMPREPLY=($(compgen -W "$(__zfs_match_explicit_snapshot) $(__zfs_list_datasets)" -- "$cur"))
                        else
                            __zfs_complete_multiple_options "$(__zfs_get_properties)" "$cur"
                        fi
                    fi
                    ;;
            esac
            ;;
        inherit)
            if ! __zfs_complete_switch "r"
            then
                __zfs_complete_ordered_arguments "$(__zfs_get_inheritable_properties)" "$(__zfs_match_explicit_snapshot) $(__zfs_list_datasets)" $cur
            fi
            ;;
        list)
            case "${prev}" in
                -d)
                    COMPREPLY=($(compgen -W "" -- "$cur"))
                    ;;
                -t)
                    __zfs_complete_multiple_options "filesystem volume snapshot all" "$cur"
                    ;;
                -o)
                    __zfs_complete_multiple_options "$(__zfs_get_properties)" "$cur"
                    ;;
                -s|-S)
                    COMPREPLY=($(compgen -W "$(__zfs_get_properties)" -- "$cur"))
                    ;;
                *)
                    if ! __zfs_complete_switch "H,r,d,o,t,s,S"
                    then
                        COMPREPLY=($(compgen -W "$(__zfs_match_explicit_snapshot) $(__zfs_list_datasets)" -- "$cur"))
                    fi
                    ;;
            esac
            ;;
        promote)
            COMPREPLY=($(compgen -W "$(__zfs_list_filesystems)" -- "$cur"))
            ;;
        rollback)
            if ! __zfs_complete_switch "r,R,f"
            then
                COMPREPLY=($(compgen -W "$(__zfs_match_snapshot)" -- "$cur"))
            fi
            ;;
        send)
            if ! __zfs_complete_switch "d,n,P,p,R,v,i,I"
            then
                COMPREPLY=($(compgen -W "$(__zfs_match_snapshot)" -- "$cur"))
            fi
            ;;
        snapshot)
            case "${prev}" in
                -o)
                    COMPREPLY=($(compgen -W "$(__zfs_get_editable_properties)" -- "$cur"))
                    ;;
                *)
                    if ! __zfs_complete_switch "o,r"
                    then
                        COMPREPLY=($(compgen -W "$(__zfs_list_datasets | awk '{print $1"@"}')" -- "$cur"))
                    fi
                    ;;
            esac
            ;;
        set)
            __zfs_complete_ordered_arguments "$(__zfs_get_editable_properties)" "$(__zfs_match_explicit_snapshot) $(__zfs_list_datasets)" $cur
            ;;
        upgrade)
            case "${prev}" in
                -a|-V|-v)
                    COMPREPLY=($(compgen -W "" -- "$cur"))
                    ;;
                *)
                    if ! __zfs_complete_switch "a,V,v,r"
                    then
                        COMPREPLY=($(compgen -W "$(__zfs_list_filesystems)" -- "$cur"))
                    fi
                    ;;
            esac
            ;;
        destroy)
            if ! __zfs_complete_switch "d,f,n,p,R,r,v"
            then
                __zfs_complete_multiple_options "$(__zfs_match_multiple_snapshots)" $cur
            fi
            ;;
        *)
            COMPREPLY=($(compgen -W "$(__zfs_match_explicit_snapshot) $(__zfs_list_datasets)" -- "$cur"))
            ;;
    esac
    __ltrim_colon_completions "$cur"
    return 0
}

__zpool_get_commands()
{
    $__ZPOOL_CMD 2>&1 | awk '/^\t[a-z]/ {print $1}' | uniq
}

__zpool_get_properties()
{
    $__ZPOOL_CMD get 2>&1 | awk '$2 == "YES" || $2 == "NO" {print $1}'; echo all
}

__zpool_get_editable_properties()
{
    $__ZPOOL_CMD get 2>&1 | awk '$2 == "YES" {print $1"="}'
}

__zpool_list_pools()
{
    $__ZPOOL_CMD list -H -o name
}

__zpool_complete()
{
    local cur prev cmd cmds
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    cmd="${COMP_WORDS[1]}"

    if [[ ${prev##*/} == zpool ]]
    then
        cmds=$(__zpool_get_commands)
        COMPREPLY=($(compgen -W "$cmds" -- "$cur"))
        return 0
    fi

    case "${cmd}" in
        get)
            __zfs_complete_ordered_arguments "$(__zpool_get_properties)" "$(__zpool_list_pools)" $cur
            return 0
            ;;
        import)
            if [[ $prev == -d ]]
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
complete -F __zpool_complete zpool
