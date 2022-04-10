bashcompletiondir = $(sysconfdir)/bash_completion.d

nodist_bashcompletion_DATA  = %D%/zfs
SUBSTFILES                 += $(nodist_bashcompletion_DATA)

SHELLCHECKSCRIPTS   += $(nodist_bashcompletion_DATA)
$(call SHELLCHECK_OPTS,$(nodist_bashcompletion_DATA)): SHELLCHECK_SHELL = bash
