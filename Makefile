#
# Makefile
#
MODULES = pg_readonly 
EXTENSION = pg_readonly  # the extension's name
DATA = pg_readonly--1.0.0.sql    # script file to install

REGRESS_OPTS =  --temp-instance=/tmp/5555 --port=5555 --temp-config pg_readonly.conf
REGRESS = test 

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir=contrib/pg_readonly
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
