CPPFLAGS ?= -Wall
CFLAGS   ?= -pipe -O2 -march=native

CPPFLAGS += -std=gnu99 -D_XOPEN_SOURCE=600 -DNDEBUG

funclub: funclub.c

%: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o '$@' '$<'

.c:
	$(CC) $(CPPFLAGS) $(CFLAGS) -o '$@' '$<'

