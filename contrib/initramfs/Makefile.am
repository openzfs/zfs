i_tdir = /usr/share/initramfs-tools
dist_i_t_SCRIPTS = \
	%D%/zfsunlock


i_t_confhooks_ddir = $(i_tdir)/conf-hooks.d
dist_i_t_confhooks_d_DATA = \
	%D%/conf-hooks.d/zfs


i_t_conf_ddir = $(i_tdir)/conf.d
dist_i_t_conf_d_DATA = \
	%D%/conf.d/zfs


i_t_hooksdir = $(i_tdir)/hooks
i_t_hooks_SCRIPTS = \
	%D%/hooks/zfs \
	%D%/hooks/zfsunlock

SUBSTFILES += $(i_t_hooks_SCRIPTS)


i_t_scriptsdir = $(i_tdir)/scripts
dist_i_t_scripts_SCRIPTS = \
	%D%/scripts/zfs


i_t_scripts_localtopdir = $(i_t_scriptsdir)/local-top
dist_i_t_scripts_localtop_SCRIPTS = \
	%D%/scripts/local-top/zfs


i_t_check_scripts    = $(dist_i_t_SCRIPTS) $(i_t_hooks_SCRIPTS) $(dist_i_t_scripts_SCRIPTS) $(dist_i_t_scripts_localtop_SCRIPTS)
SHELLCHECKSCRIPTS   += $(i_t_check_scripts)
$(call SHELLCHECK_OPTS,$(i_t_check_scripts)): SHELLCHECK_SHELL = sh


dist_noinst_DATA += %D%/README.md
