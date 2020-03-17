#!/bin/ksh

# Commands to perform failsafe-critical cleanup after a test is killed.
#
# This should only be used to ensure the system is restored to a functional
# state in the event of tests being killed (preventing normal cleanup).

zinject -c all
