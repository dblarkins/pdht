#
# makefile overlord for portals distributed hash table.
# author: d. brian larkins
# created: 3/20/16
#

#PORTALS_INCLUDEDIR=/opt/hpctools/include

CFLAGS=$(GCFLAGS) -I. -I../include -I$(PORTALS_INCLUDEDIR)

#LDFLAGS=-lportals -lpmi

RANLIB = /usr/bin/ranlib

HDRS =  pdht.h pdht_impl.h pdht_inline.h

OBJS =  assoc.o      \
        atomics.o  \
        city.o  \
        commsynch.o  \
        hash.o       \
        init.o       \
        iter.o       \
        nbputget.o   \
        pmi.o        \
        poll.o       \
        putget.o     \
        trig.o       \
        util.o       \
        # line eater

.PHONY: all
all: libpdht.a

libpdht.a: headers $(OBJS)
	mkdir -p ../lib
	$(AR) rcs ../lib/libpdht.a $(OBJS)
	$(RANLIB) ../lib/libpdht.a

$(OBJS): $(HDRS)

.PHONY: headers
headers: 
	mkdir -p ../include
	cp city.h citycrc.h pdht_inline.h pdht.h ../include/.

.PHONY: clean
clean:
	rm -f *~ *.o ../include/pdht.h ../lib/libpdht.a
