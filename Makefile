#
# Makefile
#
MODULES = pg_readonly
EXTENSION = pg_readonly  # the extension's name
DATA = pg_readonly--1.0.0.sql pg_readonly--1.0.4.sql

ifdef USE_PGXS
REGRESS_OPTS = --temp-instance=/tmp/5555 --port=5555 --temp-config pg_readonly.conf
else
REGRESS_OPTS = --temp-config $(top_srcdir)/contrib/pg_readonly/pg_readonly.conf
endif
REGRESS = test

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_readonly
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

pgxn:
	git archive --format zip  --output ../pgxn/pg_readonly/pg_readonly-1.0.3.zip master
