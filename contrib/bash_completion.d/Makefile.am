nodist_bashcompletion_DATA  = %D%/zfs %D%/zpool
COMPLETION_FILES            = %D%/zfs
SUBSTFILES                  += $(COMPLETION_FILES)
CLEANFILES                  += %D%/zpool

SHELLCHECKSCRIPTS   += $(COMPLETION_FILES)
$(call SHELLCHECK_OPTS,$(COMPLETION_FILES)): SHELLCHECK_SHELL = bash

%D%/zpool: %D%/zfs
	$(LN_S) -f zfs $@
