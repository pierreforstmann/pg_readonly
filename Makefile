MODULES = pg_readonly 
EXTENSION = pg_readonly  # the extension's name
DATA = pg_readonly--1.0.0.sql    # script file to install
#REGRESS = xxx      # the test script file

# for posgres build
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
