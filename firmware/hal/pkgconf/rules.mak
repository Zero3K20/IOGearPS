# pkgconf/rules.mak — secondary eCos repository stub
#
# eCos requires every package repository to provide pkgconf/rules.mak.
# This file delegates to the primary eCos 3.0 repository's rules.mak.
#
# ECOS_REPOSITORY is a colon-separated list of repositories; the firmware
# Makefile prepends the mainline eCos 3.0 packages path before appending
# this repository (firmware/hal), so the first entry is always the primary
# eCos packages tree (e.g. /path/to/ecos-3.0/packages).
ECOS_PRIMARY := $(firstword $(subst :, ,$(ECOS_REPOSITORY)))
include $(ECOS_PRIMARY)/pkgconf/rules.mak
