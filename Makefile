# Configure variables
CFLAGS+=-I./

# Force all to be the default target.
.PHONY: all
all:

include config.mk

# Targets needing explicit dependencies.
LIBS=slz.a
EXAMPLES=$(addprefix examples/,put get)
EXES=$(EXAMPLES)

all: $(LIBS)
slz.a: slz.o
examples/put: examples/put.o slz.a
examples/get: examples/get.o slz.a

.PHONY: examples
examples: $(EXAMPLES)


# Pattern rules
%.o: %.c flags
	@echo "  CC	$<"
ifndef VERBOSE
	@$(CC) $(CFLAGS) -c $< -o $@
else
	$(CC) $(CFLAGS) -c $< -o $@
endif

$(LIBS): %.a:
	@echo "  AR	$@"
	@rm -f $@
ifndef VERBOSE
	@$(AR) rcD $@ $^
else
	$(AR) rcD $@ $^
endif

$(EXES): %:
	@echo "  LD	$@"
ifndef VERBOSE
	@$(CCLD) $(LDFLAGS) -o $@ $^
else
	$(CCLD) $(LDFLAGS) -o $@ $^
endif


# Other miscellaneous rules
.PHONY: remake
remake: clean
	make all

# Used to force recompile if we change flags or makefiles.
.PHONY: FORCE
FORCE:

flags: new_flags FORCE
	@echo "  FLAGS"
	@{ test -f $@ && diff -q $@ $< >/dev/null; } || \
	{ echo "Flags and makefiles changed; remaking."; cp $< $@; }
	@rm new_flags

new_flags:
	@echo CC="$(CC)" > $@
	@echo CFLAGS="$(CFLAGS)" >> $@
	@echo CCLD="$(CCLD)" >> $@
	@echo LDFLAGS="$(LDFLAGS)" >> $@
	@echo AR="$(AR)" >> $@
	@md5sum Makefile config.mk >> $@


# Installation
.PHONY: install uninstall

# header files to install
HEADERS=slz.h

install: $(LIBS) $(HEADERS)
	@echo "  INSTALL"
	install -m 644 $(LIBS) $(LIB)/
	install -m 644 $(HEADERS) $(INCLUDE)/

uninstall:
	@echo "  UNINSTALL"
	rm -f $(addprefix $(LIB)/,$(LIBS))
	rm -f $(addprefix $(INCLUDE),$(HEADERS))


# Cleaning stuff.
.PHONY: depclean clean pristine

depclean:
	./depclean

clean:
	rm -f $(LIBS) $(EXES) *.o

pristine: clean depclean
	rm -f flags new_flags


# Automatic dependency generation.

# Empty dep files indicate a deleted source file; we should get rid of them.
$(shell find . -name '*.dep' -empty -print0 | xargs -0 rm -f)

%.dep: %.c flags
	@echo "  DEP	$<"
	@set -e; $(CC) -MM -MT $< $(filter-out -pedantic,$(CFLAGS)) $< |\
	sed 's,\($*\)\.c *:,\1.o $@ :,' > $@

CFILES=$(shell find . -name '*.c')

# Only include dep files if not cleaning.
ifneq (,$(filter-out depclean clean pristine, $(MAKECMDGOALS)))
include $(CFILES:.c=.dep)
else ifeq (,$(MAKECMDGOALS))
include $(CFILES:.c=.dep)
else
endif
