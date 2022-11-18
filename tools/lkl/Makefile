# SPDX-License-Identifier: GPL-2.0
# Do not use make's built-in rules
# (this improves performance and avoids hard-to-debug behaviour);
# also do not print "Entering directory..." messages from make
.SUFFIXES:
MAKEFLAGS += -r --no-print-directory

KCONFIG?=defconfig

ifneq ($(silent),1)
  ifneq ($(V),1)
	QUIET_AUTOCONF       = @echo '  AUTOCONF '$@;
	Q = @
  endif
endif

PREFIX   := /usr

ifeq (,$(srctree))
  srctree := $(patsubst %/,%,$(dir $(shell pwd)))
  srctree := $(patsubst %/,%,$(dir $(srctree)))
endif
export srctree

-include ../scripts/Makefile.include

# OUTPUT fixup should be *after* include ../scripts/Makefile.include
ifneq ($(OUTPUT),)
  OUTPUT := $(OUTPUT)/tools/lkl/
else
  OUTPUT := $(CURDIR)/
endif
export OUTPUT


all:

conf: $(OUTPUT)Makefile.conf

$(OUTPUT)Makefile.conf $(OUTPUT)include/kernel_config.h: Makefile.autoconf
	$(call QUIET_AUTOCONF, headers)$(MAKE) -f Makefile.autoconf -s

-include $(OUTPUT)Makefile.conf

export CFLAGS += -I$(OUTPUT)include -I$(srctree)/tools/lkl/include -Wall -g -O2 -Wextra \
	 -Wno-unused-parameter \
	 -Wno-missing-field-initializers -fno-strict-aliasing

export CXXFLAGS += -I$(OUTPUT)include -I$(srctree)/tools/lkl/include -Wall -g -O2 -Wextra \
	 -Wno-unused-parameter \
	 -Wno-missing-field-initializers -fno-strict-aliasing

-include Targets

TARGETS := $(progs-y:%=$(OUTPUT)%$(EXESUF))
TARGETS += $(libs-y:%=$(OUTPUT)%$(SOSUF))
all: $(TARGETS)

# this workaround is for FreeBSD
bin/stat:
ifeq ($(LKL_HOST_CONFIG_BSD),y)
	$(Q)ln -sf `which gnustat` bin/stat
	$(Q)ln -sf `which gsed` bin/sed
else
	$(Q)touch bin/stat
endif

ASM_UAPI_GENERATED:=$(OUTPUT)../../arch/lkl/include/generated/uapi/asm
ASM_CONFIG:=$(ASM_UAPI_GENERATED)/config.h
DOT_CONFIG:=$(OUTPUT)../../.config

$(DOT_CONFIG):
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) $(KCONFIG)

$(ASM_CONFIG): $(OUTPUT)include/kernel_config.h
	$(Q)mkdir -p $$(dirname $@)
	$(call QUIET_INSTALL, kernel_config.h)cp $<  $@

# rule to build lkl.o
$(OUTPUT)lib/lkl.o: bin/stat $(ASM_CONFIG) $(DOT_CONFIG)
# this workaround is for arm32 linker (ld.gold)
	$(Q)export PATH=$(srctree)/tools/lkl/bin/:${PATH} ;\
	$(MAKE) -C ../.. ARCH=lkl $(KOPT) install INSTALL_PATH=$(OUTPUT)

# rules to link libs
$(OUTPUT)%$(SOSUF): LDFLAGS += -shared
$(OUTPUT)%$(SOSUF): $(OUTPUT)%-in.o $(OUTPUT)liblkl.a
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LDFLAGS_$*-y) -o $@ $^ $(LDLIBS) $(LDLIBS_$*-y)

# liblkl is special
$(OUTPUT)liblkl$(SOSUF): $(OUTPUT)%-in.o $(OUTPUT)lib/lkl.o
$(OUTPUT)liblkl.a: $(OUTPUT)lib/liblkl-in.o $(OUTPUT)lib/lkl.o
	$(QUIET_AR)$(AR) -rc $@ $^

# Extra linker flags for LKL based fuzzers
$(OUTPUT)fuzzers/%$(EXESUF): LDFLAGS += -fsanitize=fuzzer --coverage


ifeq ($(LKL_VERBOSE_BUILD),1)
  LKL_LD_VERBOSE := --verbose -Wl,--verbose -Wl,-t
endif

# rule to link programs
# To enable support for kernel symbols we need to update kallsyms symbols when
# building the final executable (b/167584250). We do this in a similar way to
# how kernel symbols are generated in `${srctree}/scripts/link-vmlinux.sh`.
$(OUTPUT)%$(EXESUF): $(OUTPUT)%-in.o $(OUTPUT)liblkl.a
	# First, build the executable and extract symbols into and object file $@.1.o
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LDFLAGS_$*-y) -o $@.1 $^ $(LDLIBS) $(LDLIBS_$*-y) $(LKL_LD_VERBOSE)
	$(Q)$(srctree)/tools/lkl/scripts/gen_kallsyms.sh $@.1 $@.1.S $@.1.o $(OUTPUT)../../ $(srctree)
	# Then, build the executable again with linking the object file with kallsyms generated above
	# and extract symbols from the resulting executable one more time.
	# This is needed to account for potential changes in symbol values due
	# to linking an extra object file with symbols.
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LDFLAGS_$*-y) -o $@.2 $@.1.o $^ $(LDLIBS) $(LDLIBS_$*-y) $(LKL_LD_VERBOSE)
	$(Q)$(srctree)/tools/lkl/scripts/gen_kallsyms.sh $@.2 $@.2.S $@.2.o $(OUTPUT)../../ $(srctree)
	# Finaly, build the target executable $@ with kallsyms symbols $@.2.o.
	# It is expected that the size of $@ is exactly the same as $@.2 (per
	# comments in `${srctree}/scripts/link-vmlinux.sh` sometimes this might
	# not be the case and an extra iteration might be required).
	$(QUIET_LINK)$(CC) $(LDFLAGS) $(LDFLAGS_$*-y) -o $@ $@.2.o $^ $(LDLIBS) $(LDLIBS_$*-y) $(LKL_LD_VERBOSE)

# rule to build objects
$(OUTPUT)%-in.o: $(OUTPUT)lib/lkl.o FORCE
	$(Q)$(MAKE) -f $(srctree)/tools/build/Makefile.build dir=$(patsubst %/,%,$(dir $*)) obj=$(notdir $*)


$(OUTPUT)cpfromfs$(EXESUF): cptofs$(EXESUF)
	$(Q)if ! [ -e $@ ]; then ln -s $< $@; fi

clean:
	$(call QUIET_CLEAN, objects)find $(OUTPUT) -name '*.o' -delete -o -name '\.*.cmd'\
	 -delete -o -name '\.*.d' -delete
	$(call QUIET_CLEAN, headers)$(RM) -r $(OUTPUT)/include/lkl/
	$(call QUIET_CLEAN, liblkl.a)$(RM) $(OUTPUT)/liblkl.a
	$(call QUIET_CLEAN, targets)$(RM) $(TARGETS) bin/stat

clean-conf: clean
	$(call QUIET_CLEAN, Makefile.conf)$(RM) $(OUTPUT)/Makefile.conf

headers_install: $(TARGETS)
	$(call QUIET_INSTALL, headers) \
	    install -d $(DESTDIR)$(PREFIX)/include ; \
	    install -m 644 include/lkl.h include/lkl_host.h $(OUTPUT)include/lkl_autoconf.h \
	      include/lkl_config.h $(DESTDIR)$(PREFIX)/include ; \
	    cp -r $(OUTPUT)include/lkl $(DESTDIR)$(PREFIX)/include

libraries_install: $(libs-y:%=$(OUTPUT)%$(SOSUF)) $(OUTPUT)liblkl.a
	$(call QUIET_INSTALL, libraries) \
	    install -d $(DESTDIR)$(PREFIX)/lib ; \
	    install -m 644 $^ $(DESTDIR)$(PREFIX)/lib

programs_install: $(progs-y:%=$(OUTPUT)%$(EXESUF))
	$(call QUIET_INSTALL, programs) \
	    install -d $(DESTDIR)$(PREFIX)/bin ; \
	    install -m 755 $^ $(DESTDIR)$(PREFIX)/bin

install: headers_install libraries_install programs_install


run-tests:
	./tests/run.py $(tests)

FORCE: ;
.PHONY: all clean FORCE run-tests
.PHONY: headers_install libraries_install programs_install install
.NOTPARALLEL : lib/lkl.o
.SECONDARY:

