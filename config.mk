# Variables affecting installation.
PREFIX=/usr/local

# Variables affecting compilation.
CC=gcc
CCLD=$(CC)
AR=ar
CFLAGS+= -std=c99 -pedantic -Wall -Wextra -Werror -pipe
LDFLAGS+=

CFLAGS_DEBUG= -O0 -ggdb3
CFLAGS_RELEASE= -O3 -fomit-frame-pointer -DNDEBUG -DSLZ_RELEASE
# feel free to mess around with this.
CFLAGS_CUSTOM=

# Default to debug.
ifeq (,$(MODE))
MODE=debug
endif

ifeq (debug,$(MODE))
CFLAGS+= $(CFLAGS_DEBUG)
else ifeq (release,$(MODE))
CFLAGS+= $(CFLAGS_RELEASE)
else ifeq (custom,$(MODE))
CFLAGS+= $(CFLAGS_CUSTOM)
else
$(error "unknown build mode: $(MODE)")
endif


# For building with clang. Notably, don't have to change any compilation flags.
# NB. Compiling with clang gives nicer compilation error messages, but forfeits
# the ability to use macros from gdb.
#CC=clang
