.POSIX:

.SUFFIXES:
.SUFFIXES: .c .o

# Flags

CC = cc
CFLAGS = -Wall -Wextra --std=c99 \
	-DSQLITE_DQS=0 -DSQLITE_THREADSAFE=0 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_LIKE_DOESNT_MATCH_GLOBS -DSQLITE_MAX_EXPR_DEPTH=0 -DSQLITE_OMIT_DECLTYPE -DSQLITE_OMIT_DEPRECATED -DSQLITE_OMIT_PROGRESS_CALLBACK -DSQLITE_OMIT_SHARED_CACHE -DSQLITE_USE_ALLOCA -DSQLITE_OMIT_AUTOINIT \
	-Oz -flto -fmerge-all-constants -fno-asynchronous-unwind-tables -fno-ident -fno-stack-protector -fno-unwind-tables
LDFLAGS = -flto
DEPFLAGS = -MT $@ -MMD -MP -MF $*.d

PACKAGE = zsql
VERSION = 1.0.0

# Files

C = args.c fuzzy_match.c migrate.c sqlh.c sqlite3.c utf8.c zsql.c
DOC = zsql.1

EXECFILE = zsql
MAKEFILE = Makefile
PACKFILE = $(PACKAGE)-$(VERSION).tar

# Targets

all: compile

clean:
	rm -f $(C:.c=.o) $(C:.c=.d) $(EXECFILE) $(PACKFILE)

compile: $(EXECFILE)

lint:
	clang-tidy --quiet '--checks=-*,bugprone-*,clang-analyzer-*,clang-diagnostic-*,performance-*,portability-*,readability-*,-readability-magic-numbers' '--header-filter=(?!sqlite3.h).*' $(C:sqlite3.c=) -- -Weverything $(CFLAGS) $(LDFLAGS)
	cppcheck --quiet --enable=all --force $(C:sqlite3.c=)

package: $(PACKFILE)

# Outputs

$(EXECFILE): $(C:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $(C:.c=.o)
	strip $@

$(PACKFILE): $(EXECFILE) $(DOC)
	tar cf $@ $(EXECFILE) $(DOC)

# Relations

-include $(C:.c=.d)

# Suffix rules

.c.o:
	$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<
