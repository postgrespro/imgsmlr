# imgsmlr/Makefile

MODULE_big = imgsmlr
OBJS = imgsmlr.o imgsmlr_idx.o
EXTENSION = imgsmlr
DATA = imgsmlr--1.0.sql
#PG_CPPFLAGS = -lgd
SHLIB_LINK = -lgd

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

