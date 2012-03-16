.PHONY: all FORCE
all:
FORCE:

include config.mk

LIBS=srlz.a
all: $(LIBS)

SRLZ_SRCS=srlz
rvmi: $(addsuffix .o, $(RVMI_SRCS))

# Pattern rules
%.o: %.c flags
	@echo "  CC	$<"
ifdef VERBOSE
	@$(CC) $(CFLAGS) -c $< -o $@
else
	$(CC) $(CFLAGS) -c $< -o $@
endif

$(LIBS): %.a:
	@echo "  AR	$^"
	@rm $@
ifdef VERBOSE
	@$(AR) "rcDs$(ARFLAGS)" $@ $^
else
	$(AR) "rcDs$(ARFLAGS)" $@ $^
endif

# Other miscellaneous rules
.PHONY: remake
remake: clean
	make all

# Used to force recompile if we change flags or makefiles.
flags: new_flags FORCE
	@echo "  FLAGS"
	@{ test -f $@ && diff -q $@ $< >/dev/null; } || \
	{ echo "Flags and makefiles changed; remaking."; cp $< $@; }
	@rm new_flags

new_flags:
	@echo CC="$(CC)" > $@
	@echo CFLAGS="$(CFLAGS)" >> $@
	@echo LDFLAGS="$(LDFLAGS)" >> $@
	@echo ARFLAGS="$(ARFLAGS)" >> $@
	@md5sum Makefile config.mk >> $@


# Cleaning stuff.
.PHONY: depclean clean pristine

depclean:
	./depclean

clean:
	rm -f $(LIBS) *.o

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
