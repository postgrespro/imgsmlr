# imgsmlr/Makefile

MODULE_big = imgsmlr
OBJS = imgsmlr.o imgsmlr_idx.o
EXTENSION = imgsmlr
DATA = imgsmlr--1.0.sql
SHLIB_LINK = -lgd
REGRESS = imgsmlr
EXTRA_CLEAN = data/*.hex


ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/imgsmlr
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

ifdef USE_ASSERT_CHECKING
override CFLAGS += -DUSE_ASSERT_CHECKING
endif

installcheck: data/1.jpg.hex data/2.png.hex data/3.gif.hex data/4.jpg.hex data/5.png.hex data/6.gif.hex data/7.jpg.hex data/8.png.hex data/9.gif.hex data/10.jpg.hex data/11.png.hex data/12.gif.hex

data/%.hex: data/%
	xxd -p $< > $@

maintainer-clean:
	rm -f data/*.hex