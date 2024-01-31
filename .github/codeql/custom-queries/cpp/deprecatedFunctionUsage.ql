/**
 * @name Deprecated function usage detection
 * @description Detects functions whose usage is banned from the OpenZFS
 *              codebase due to QA concerns.
 * @kind problem
 * @severity error
 * @id cpp/deprecated-function-usage
*/

import cpp

predicate isDeprecatedFunction(Function f) {
  f.getName() = "strtok" or
  f.getName() = "__xpg_basename" or
  f.getName() = "basename" or
  f.getName() = "dirname" or
  f.getName() = "bcopy" or
  f.getName() = "bcmp" or
  f.getName() = "bzero" or
  f.getName() = "asctime" or
  f.getName() = "asctime_r" or
  f.getName() = "gmtime" or
  f.getName() = "localtime" or
  f.getName() = "strncpy"

}

string getReplacementMessage(Function f) {
  if f.getName() = "strtok" then
    result = "Use strtok_r(3) instead!"
  else if f.getName() = "__xpg_basename" then
    result = "basename(3) is underspecified. Use zfs_basename() instead!"
  else if f.getName() = "basename" then
    result = "basename(3) is underspecified. Use zfs_basename() instead!"
  else if f.getName() = "dirname" then
    result = "dirname(3) is underspecified. Use zfs_dirnamelen() instead!"
  else if f.getName() = "bcopy" then
    result = "bcopy(3) is deprecated. Use memcpy(3)/memmove(3) instead!"
  else if f.getName() = "bcmp" then
    result = "bcmp(3) is deprecated. Use memcmp(3) instead!"
  else if f.getName() = "bzero" then
    result = "bzero(3) is deprecated. Use memset(3) instead!"
  else if f.getName() = "asctime" then
    result = "Use strftime(3) instead!"
  else if f.getName() = "asctime_r" then
    result = "Use strftime(3) instead!"
  else if f.getName() = "gmtime" then
    result = "gmtime(3) isn't thread-safe. Use gmtime_r(3) instead!"
  else if f.getName() = "localtime" then
    result = "localtime(3) isn't thread-safe. Use localtime_r(3) instead!"
  else
    result = "strncpy(3) is deprecated. Use strlcpy(3) instead!"
}

from FunctionCall fc, Function f
where
  fc.getTarget() = f and
  isDeprecatedFunction(f)
select fc, getReplacementMessage(f)
