# supplementary Makefile for linux-2.4.31

# need to figure out a way to do this...
CONFIG_FILE=linux-2.4.31/config.production
#CONFIG_FILE=linux-2.4.31/config.nfsroot

KERNEL_PATCHES = architecture.patch squashfs2.2-patch smc91111.patch ppc405-wdt.patch kexec-ppc-2.4.patch mvp-version.patch clockfix.patch sdram-bank1.patch memsize.patch

ifeq (x86_64, $(shell uname -p))
	KERNEL_PATCHES += x86_64-host.patch
endif

ifeq (Darwin, $(shell uname -s))
	KERNEL_PATCHES += macx-host.patch
endif

# track patches we've applied, so they don't get reapplied
$(KERNEL_SOURCE)/.patched: $(KERNEL_SOURCE)/Makefile
	@cp linux-2.4.31/redwood.c $(KERNEL_SOURCE)/drivers/mtd/maps/redwood.c
	@for patchfile in $(KERNEL_PATCHES) ; do \
		patch -f -N -s -p1 -d $(KERNEL_SOURCE) < linux-2.4.31/$$patchfile; \
	done \
# disable genksyms and depmod, they just cause unnecessary pain
# NOTE: do NOT turn module versioning on! It's just not necessary
# and adds more bloat to the kernel than it's worth
	@sed -i -e "s,^GENKSYMS\W*=.*$$,GENKSYMS = $(BINTRUE)," $(KERNEL_SOURCE)/Makefile
	@sed -i -e "s,^DEPMOD\W*=.*$$,DEPMOD = $(BINTRUE)," $(KERNEL_SOURCE)/Makefile
# add our local version so it's reflected in the kernel build
	@sed -i -e "s,^EXTRAVERSION\W*=.*$$,EXTRAVERSION = $(KERN_EXT)," $(KERNEL_SOURCE)/Makefile
# add install path
	@sed -i -e "/#export\W*INSTALL_PATH/c\export INSTALL_PATH = $(MVP_IMAGE)" $(KERNEL_SOURCE)/Makefile
# link arch/ppc/boot/simple/dongle_version.h
	@ln -s ${CURDIR}/dongle_version.h $(KERNEL_SOURCE)/include/dongle_version.h
	@touch $(KERNEL_SOURCE)/.patched


# modversions.h isn't handled too gracefully, so we just stuff it in here
$(KERNEL_SOURCE)/.config: $(KERNEL_SOURCE)/.patched
	@cp $(CONFIG_FILE) $(KERNEL_SOURCE)/.config
	@${MAKE} -C $(KERNEL_SOURCE) oldconfig
	@${MAKE} -C $(KERNEL_SOURCE) $(KERNEL_SOURCE)/include/linux/modversions.h
