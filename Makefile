HEADERS=slz.h
SOURCES=slz.c
LIBS=libslz.a
EXAMPLES=$(addprefix examples/,put get)
EXES=$(EXAMPLES)
BUILD_FILES=Makefile config.mk depclean
TAR_FILES=$(BUILD_FILES) $(SOURCES) $(HEADERS) $(addsuffix .c, $(EXAMPLES))

# libslz.a is default target.
libslz.a: $(SOURCES:.c=.o)

include config.mk

# Tarballs.
slz.tar.gz: $(TAR_FILES)
slz.tar.bz2: $(TAR_FILES)

# Examples.
.PHONY: examples
examples: $(EXAMPLES)
$(EXAMPLES): %: %.o $(LIBS)
# Examples need `#include <slz.h>' to work
$(EXAMPLES): CFLAGS+=-I./


# Pattern rules
%.o: %.c flags
	@echo "   CC	$<"
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBS): %.a:
	@echo "   AR	$@"
	rm -f $@
	$(AR) rcD $@ $^

$(EXES): %:
	@echo "   LD	$@"
	$(CCLD) $(LDFLAGS) -o $@ $^

%.tar.gz:
	@echo "   TAR	$@"
	tar czf $@ $^

%.tar.bz2:
	@echo "   TAR	$@"
	tar cjf $@ $^


# Used to force recompile if we change flags or makefiles.
.PHONY: FORCE
FORCE:

flags: new_flags FORCE
	@{ test -f $@ && diff -q $@ $< >/dev/null; } || \
	{ echo "Flags and makefiles changed; remaking."; cp $< $@; }
	@rm new_flags

new_flags:
	@echo CC="$(CC)" > $@
	@echo CFLAGS="$(CFLAGS)" >> $@
	@echo CCLD="$(CCLD)" >> $@
	@echo LDFLAGS="$(LDFLAGS)" >> $@
	@echo AR="$(AR)" >> $@
	@md5sum Makefile >> $@


# Installation
.PHONY: install uninstall

# header files to install
install: $(LIBS) $(HEADERS)
	@echo "   INSTALL"
	install -m 644 $(LIBS) $(LIB)/
	install -m 644 $(HEADERS) $(INCLUDE)/

uninstall:
	@echo "   UNINSTALL"
	rm -f $(addprefix $(LIB)/,$(LIBS))
	rm -f $(addprefix $(INCLUDE)/,$(HEADERS))


# Cleaning stuff.
CLEAN_RULES=nodeps clean pristine
.PHONY: $(CLEAN_RULES)

nodeps:
	./depclean

clean:
	@echo "   CLEAN"
	find . -name '*.o' -delete
	rm -f $(LIBS) $(EXES) slz.tar.*

pristine: clean nodeps
	rm -f flags new_flags


# Automatic dependency generation.

# Empty dep files indicate a deleted source file; we should get rid of them.
$(shell find . -name '*.dep' -empty -print0 | xargs -0 rm -f)

%.dep: %.c flags
	@echo "   DEP	$<"
	set -e; $(CC) -MM -MT $< $(filter-out -pedantic,$(CFLAGS)) $< |\
	sed 's,\($*\)\.c *:,\1.o $@ :,' > $@

CFILES=$(shell find . -name '*.c')

# Only include dep files in certain circumstances.
NODEP_RULES=$(CLEAN_RULES) slz.tar.% uninstall

ifneq (,$(filter-out $(NODEP_RULES), $(MAKECMDGOALS)))
include $(CFILES:.c=.dep)
else ifeq (,$(MAKECMDGOALS))
include $(CFILES:.c=.dep)
else
endif
