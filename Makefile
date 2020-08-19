.POSIX:

.SUFFIXES:
.SUFFIXES: .c .o

# Flags

CC = cc
CFLAGS = -Wall -Wextra --std=c11 -Og -fsanitize=undefined -fsanitize-trap=undefined # -Oz -flto -fmerge-all-constants -fno-asynchronous-unwind-tables -fno-ident -fno-stack-protector -fno-unwind-tables
LDFLAGS = # -flto
DEPFLAGS = -MT $@ -MMD -MP -MF $*.d

PACKAGE = zsql
VERSION = 1.0.0

# Files

C = args.c fuzzy_match.c migrate.c sqlh.c sqlite3.c utf8.c zsql.c
DOC = zsql.1
EXECUTABLE = zsql
MAKEFILE = Makefile

# Targets

all: compile

clean:
	rm -f $(C:.c=.o) $(C:.c=.d) $(EXECUTABLE) $(PACKAGE)-$(VERSION).tar

compile: $(EXECUTABLE)

package: $(PACKAGE)-$(VERSION).tar

# Outputs

$(EXECUTABLE): $(C:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $(C:.c=.o)
	strip $@

$(PACKAGE)-$(VERSION).tar: $(EXECUTABLE) $(DOC)
	tar -cf $@ $(EXECUTABLE) $(DOC)

# Relations

-include $(C:.c=.d)

# Suffix rules

.c.o:
	$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<
