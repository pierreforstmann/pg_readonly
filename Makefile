#
# Makefile
#
MODULES = pg_readonly 
EXTENSION = pg_readonly  # the extension's name
DATA = pg_readonly--1.0.0.sql    # script file to install

# make installcheck to run automatic test
REGRESS_OPTS =  --temp-instance=/tmp/5555 --port=5555 --temp-config pg_readonly.conf
REGRESS = test 

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

pgxn:
	git archive --format zip  --output ../pgxn/pg_readonly/pg_readonly-1.0.2.zip main
