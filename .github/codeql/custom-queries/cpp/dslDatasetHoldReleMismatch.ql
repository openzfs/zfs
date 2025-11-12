/**
 * @name Detect mismatched dsl_dataset_hold/_rele pairs
 * @description Flags instances of issue #12014 where
 *   - a dataset held with dsl_dataset_hold_obj() ends up in dsl_dataset_rele_flags(), or
 *   - a dataset held with dsl_dataset_hold_obj_flags() ends up in dsl_dataset_rele().
 * @kind problem
 * @severity error
 * @tags correctness
 * @id cpp/dslDatasetHoldReleMismatch
 */

import cpp

from Variable ds, Call holdCall, Call releCall, string message
where
    ds.getType().toString() = "dsl_dataset_t *" and
    holdCall.getASuccessor*() = releCall and
    (
        (holdCall.getTarget().getName() = "dsl_dataset_hold_obj_flags" and
         holdCall.getArgument(4).(AddressOfExpr).getOperand().(VariableAccess).getTarget() = ds and
         releCall.getTarget().getName() = "dsl_dataset_rele" and
         releCall.getArgument(0).(VariableAccess).getTarget() = ds and
         message = "Held with dsl_dataset_hold_obj_flags but released with dsl_dataset_rele")
        or
        (holdCall.getTarget().getName() = "dsl_dataset_hold_obj" and
         holdCall.getArgument(3).(AddressOfExpr).getOperand().(VariableAccess).getTarget() = ds and
         releCall.getTarget().getName() = "dsl_dataset_rele_flags" and
         releCall.getArgument(0).(VariableAccess).getTarget() = ds and
         message = "Held with dsl_dataset_hold_obj but released with dsl_dataset_rele_flags")
    )
select releCall,
       "Mismatched release: held with $@ but released with " + releCall.getTarget().getName() + " for dataset $@",
       holdCall, holdCall.getTarget().getName(),
       ds, ds.toString()
