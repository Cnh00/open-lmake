# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#
# build configuration
#vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

MAKEFLAGS = -j8 -r -R -k

CC := gcc
#CC := gcc --coverage
#CC := clang                                                                   # for some unknown reason, clang is incompatible with -fsanitize

# -fsanitize=address and -fsanitize=thread are exclusive of one another
#ASAN_FLAGS := -fsanitize=address -fsanitize=undefined
#ASAN_FLAGS := -fsanitize=address
#TSAN_FLAGS := -fsanitize=thread

#OPT_FLAGS := -O0 -g
OPT_FLAGS := -O3
#OPT_FLAGS := -O3 -DNDEBUG

#^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#

WARNING_FLAGS := -Wall -Wextra
ifeq ($(strip $(CC)),clang)
WARNING_FLAGS += -Wno-misleading-indentation -Wno-unknown-warning-option -Wno-c2x-extensions
endif

# python configuration
PYTHON := $(shell python3 -c 'import sys ; print(sys.executable)' 2>/dev/null )
ifeq ($(PYTHON),)
$(error cannot find python3)
endif

SAN_FLAGS          := $(strip $(ASAN_FLAGS) $(TSAN_FLAGS))
SAN                := $(if $(SAN_FLAGS),.san,)
ASAN               := $(if $(findstring address,$(SAN_FLAGS)),.san,)
TSAN               := $(if $(findstring thread,$(SAN_FLAGS)),.san,)
PREPROCESS         := $(CC) -E
COMPILE            := $(CC) -c -fvisibility=hidden
LINK_O             := $(CC) -r
LINK_SO            := $(CC) -shared-libgcc -shared -pthread
LINK_BIN           := $(CC) -pthread
LINK_LIB           := -ldl -lstdc++ -lm
PYTHON_INCLUDE_DIR := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_path      ("include"  )      )')
PYTHON_LIB_BASE    := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_config_var("LDLIBRARY")[3:-3])') # [3:-3] : transform lib<foo>.so -> <foo>
PYTHON_LIB_DIR     := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_config_var("LIBDIR"   )      )')
PYTHON_VERSION     := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_config_var("VERSION"  )      )')
CFLAGS             := $(OPT_FLAGS) -fno-strict-aliasing -pthread -pedantic $(WARNING_FLAGS) -Werror
CXXFLAGS           := $(CFLAGS) -std=c++20
ROOT               := $(shell pwd)
LIB                := lib
SLIB               := _lib
BIN                := bin
SBIN               := _bin
DOC                := doc
SRC                := src
LMAKE_ENV          := lmake_env
STORE_LIB          := $(SRC)/store

# PYCXX
PYCXX             := ext/pycxx-7.1.7.patched
PYCXX_ROOT        := $(PYCXX)/pycxx-7.1.7
PYCXX_HOME        := $(PYCXX_ROOT)/home
PYCXX_LIB         := $(PYCXX_HOME)/lib
PYCXX_CXX         := $(PYCXX_HOME)/share/python$(PYTHON_VERSION)/CXX
PYCXX_INCLUDE_DIR := $(PYCXX_HOME)/include/python

# SECCOMP
SECCOMP             := ext/libseccomp-2.1.0.dir
SECCOMP_ROOT        := $(SECCOMP)/libseccomp-2.1.0
SECCOMP_INCLUDE_DIR := $(SECCOMP_ROOT)/include
SECCOMP_LIB_DIR     := $(SECCOMP_ROOT)/src

# Engine
ENGINE_LIB := $(SRC)/lmakeserver

# LMAKE
LMAKE_FILES = \
	$(DOC)/lmake_doc.pptx         \
	$(DOC)/lmake.html             \
	unit_tests/base/Lmakefile.py  \
	$(SLIB)/autodep_ld_audit.so   \
	$(SLIB)/autodep_ld_preload.so \
	$(SLIB)/read_makefiles.py     \
	$(SLIB)/serialize.py          \
	$(SBIN)/job_exec              \
	$(SBIN)/lmakeserver           \
	$(SBIN)/ldump                 \
	$(SBIN)/ldump_job             \
	$(LIB)/lmake.py               \
	$(LIB)/clmake.so              \
	$(BIN)/autodep                \
	$(BIN)/lcheck_deps            \
	$(BIN)/lcritical_barrier      \
	$(BIN)/ldepend                \
	$(BIN)/ldep_crcs              \
	$(BIN)/lforget                \
	$(BIN)/lfreeze                \
	$(BIN)/lmake                  \
	$(BIN)/lshow                  \
	$(BIN)/ltarget                \
	$(BIN)/lunlink                \
	$(BIN)/xxhsum

DFLT : LMAKE UNIT_TESTS LMAKE_TEST lmake.tar.gz

ALL : DFLT STORE_TEST $(DOC)/lmake.html

sys_config.h : sys_config
	CC=$(CC) PYTHON=$(PYTHON) ./$< > $@

lmake.tar.gz : $(LMAKE_FILES)
	tar -cz -f $@ $^

EXT : $(PYCXX).test.stamp

ext/%.dir.stamp : ext/%.tar.gz
	@rm -rf $(@:%.stamp=%)
	@mkdir -p $(@:%.stamp=%)
	tar -zxf $< -C $(@:%.stamp=%)
	touch $@
ext/%.dir.stamp : ext/%.zip
	@rm -rf $(@:%.stamp=%)
	@mkdir -p $(@:%.stamp=%)
	unzip -d $(@:%.stamp=%) $<
	touch $@
ext/%.patched.stamp : ext/%.dir.stamp ext/%.patch_script
	rm -rf $(@:%.stamp=%)
	cp -r $(@:%.patched.stamp=%.dir) $(@:%.patched.stamp=%.patched)
	cd $(@:%.patched.stamp=%.patched) ; $(ROOT)/$(@:%.patched.stamp=%.patch_script)
	touch $@
ext/%.patched.h : ext/%.h ext/%.patch_script
	cp $< $@
	cd $(@D) ; $(ROOT)/$(@:%.patched.h=%.patch_script) $(@F)

.SECONDARY :

%.html : %.texi ; texi2any --html --no-split --output=$@ $<

#
# LMAKE
#

LMAKE : $(LMAKE_FILES)

#
# PYCXX
#

$(PYCXX).install.stamp : $(PYCXX).stamp
	rm -rf $(PYCXX_HOME)
	cd $(PYCXX_ROOT) ; $(PYTHON) setup.py install --home=$(ROOT)/$(PYCXX_HOME)
	mv $(PYCXX_CXX)/cxxextensions.c $(PYCXX_CXX)/cxxextensions.cxx             # painful to provide compilation rules for both .c and .cxx
	ln -s . $(PYCXX_CXX)/Src                                                   # support files include files starting with Src which is not installed
	touch $@
$(PYCXX_LIB)/%$(SAN).o : $(PYCXX).install.stamp
	$(COMPILE) $(CXXFLAGS) $(SAN_FLAGS) -fPIC -I $(PYCXX_INCLUDE_DIR) -I $(PYTHON_INCLUDE_DIR) -o $@ $(@:$(PYCXX_LIB)/%$(SAN).o=$(PYCXX_CXX)/%.cxx)
$(PYCXX).test.stamp : $(PYCXX).stamp
	cd $(@:%.test.stamp=%.patched) ; $(PYTHON) setup_makefile.py linux Makefile
	cd $(@:%.test.stamp=%.patched) ; make clean ; make -j8 test
	touch $@
$(PYCXX_LIB)/pycxx$(SAN).o : $(patsubst %,$(PYCXX_LIB)/%$(SAN).o, cxxsupport cxx_extensions cxx_exceptions cxxextensions IndirectPythonInterface )
	$(LINK_O) $(SAN_FLAGS) -fPIC -o $@ $^

#
# SECCOMP
#

$(SECCOMP).install.stamp : $(SECCOMP).stamp
	cd $(SECCOMP_ROOT) ; \
	./configure ; \
	MAKEFLAGS= make -j8
	touch $@

#
# store
#

STORE_TEST : $(STORE_LIB)/unit_test.tok

$(STORE_LIB)/unit_test : \
	$(STORE_LIB)/file$(SAN).o \
	$(SRC)/app$(SAN).o        \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
	$(STORE_LIB)/unit_test$(SAN).o
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(STORE_LIB)/unit_test.tok : $(STORE_LIB)/unit_test
	rm -rf $(STORE_LIB)/unit_test.dir
	./$< $(STORE_LIB)/unit_test.dir
	touch $@

#
# engine
#

INCLUDE_SECCOMP := -I $(SECCOMP_INCLUDE_DIR)
INSTALL_SECCOMP := $(SECCOMP).install.stamp

SLIB_H    := $(patsubst %, $(SRC)/%.hh         , app client config disk hash lib non_portable pycxx rpc_client rpc_job serialize time trace utils )
AUTODEP_H := $(patsubst %, $(SRC)/autodep/%.hh , autodep_ld autodep_support gather_deps ptrace record                                             )
STORE_H   := $(patsubst %, $(SRC)/store/%.hh   , alloc file prefix red_black side_car struct vector                                               )
ENGINE_H  := $(patsubst %, $(ENGINE_LIB)/%.hh  , backend.x cmd.x core core.x global.x job.x makefiles node.x req.x rule.x store.x                 )

ALL_TOP_H    := sys_config.h $(SLIB_H) $(AUTODEP_H) $(PYCXX).install.stamp $(INSTALL_SECCOMP) ext/xxhash.patched.h
ALL_ENGINE_H := $(ALL_TOP_H) $(ENGINE_H) $(STORE_H)

INCLUDES := -I $(SRC) -I $(ENGINE_LIB) -I ext -I $(PYTHON_INCLUDE_DIR) -I $(PYCXX_INCLUDE_DIR) $(INCLUDE_SECCOMP) -I.
%.san.i : %.cc $(ALL_ENGINE_H) ; $(PREPROCESS) $(CXXFLAGS) $(SAN_FLAGS)              $(INCLUDES) -o $@ $<
%.san.o : %.cc $(ALL_ENGINE_H) ; $(COMPILE)    $(CXXFLAGS) $(SAN_FLAGS) -frtti -fPIC $(INCLUDES) -o $@ $<
%.i     : %.cc $(ALL_ENGINE_H) ; $(PREPROCESS) $(CXXFLAGS)                           $(INCLUDES) -o $@ $<
%.o     : %.cc $(ALL_ENGINE_H) ; $(COMPILE)    $(CXXFLAGS)              -frtti -fPIC $(INCLUDES) -o $@ $<

#
# lmake
#

LIB_SECCOMP := -L $(SECCOMP_LIB_DIR) -lseccomp

$(SBIN)/lmakeserver : \
	$(PYCXX_LIB)/pycxx$(SAN).o                \
	$(SRC)/app$(SAN).o                        \
	$(SRC)/disk$(SAN).o                       \
	$(SRC)/hash$(SAN).o                       \
	$(SRC)/lib$(SAN).o                        \
	$(SRC)/non_portable.o                     \
	$(SRC)/pycxx$(SAN).o                      \
	$(SRC)/rpc_client$(SAN).o                 \
	$(SRC)/rpc_job$(SAN).o                    \
	$(SRC)/time$(SAN).o                       \
	$(SRC)/trace$(SAN).o                      \
	$(SRC)/utils$(SAN).o                      \
	$(SRC)/store/file$(SAN).o                 \
	$(SRC)/autodep/gather_deps$(SAN).o        \
	$(SRC)/autodep/ptrace$(SAN).o             \
	$(SRC)/autodep/record$(SAN).o             \
	$(SRC)/lmakeserver/backend$(SAN).o        \
	$(SRC)/lmakeserver/backends/local$(SAN).o \
	$(SRC)/lmakeserver/cmd$(SAN).o            \
	$(SRC)/lmakeserver/global$(SAN).o         \
	$(SRC)/lmakeserver/job$(SAN).o            \
	$(SRC)/lmakeserver/makefiles$(SAN).o      \
	$(SRC)/lmakeserver/node$(SAN).o           \
	$(SRC)/lmakeserver/req$(SAN).o            \
	$(SRC)/lmakeserver/rule$(SAN).o           \
	$(SRC)/lmakeserver/store$(SAN).o          \
	$(SRC)/lmakeserver/main$(SAN).o
	mkdir -p $(@D)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ -L$(PYTHON_LIB_DIR) -l$(PYTHON_LIB_BASE) $(LIB_SECCOMP) $(LINK_LIB)

$(SBIN)/ldump : \
	$(PYCXX_LIB)/pycxx$(SAN).o         \
	$(SRC)/app$(SAN).o                 \
	$(SRC)/disk$(SAN).o                \
	$(SRC)/hash$(SAN).o                \
	$(SRC)/lib$(SAN).o                 \
	$(SRC)/non_portable.o              \
	$(SRC)/pycxx$(SAN).o               \
	$(SRC)/rpc_client$(SAN).o          \
	$(SRC)/rpc_job$(SAN).o             \
	$(SRC)/time$(SAN).o                \
	$(SRC)/trace$(SAN).o               \
	$(SRC)/utils$(SAN).o               \
	$(SRC)/store/file$(SAN).o          \
	$(SRC)/lmakeserver/backend$(SAN).o \
	$(SRC)/lmakeserver/global$(SAN).o  \
	$(SRC)/lmakeserver/job$(SAN).o     \
	$(SRC)/lmakeserver/node$(SAN).o    \
	$(SRC)/lmakeserver/req$(SAN).o     \
	$(SRC)/lmakeserver/rule$(SAN).o    \
	$(SRC)/lmakeserver/store$(SAN).o   \
	$(SRC)/ldump$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ -L$(PYTHON_LIB_DIR) -l$(PYTHON_LIB_BASE) $(LINK_LIB)

$(SBIN)/ldump_job : \
	$(SRC)/app$(SAN).o     \
	$(SRC)/disk$(SAN).o    \
	$(SRC)/hash$(SAN).o    \
	$(SRC)/lib$(SAN).o     \
	$(SRC)/non_portable.o  \
	$(SRC)/rpc_job$(SAN).o \
	$(SRC)/time$(SAN).o    \
	$(SRC)/trace$(SAN).o   \
	$(SRC)/utils$(SAN).o   \
	$(SRC)/ldump_job$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ -L$(PYTHON_LIB_DIR) -l$(PYTHON_LIB_BASE) $(LINK_LIB)

$(SBIN)/job_exec : \
	$(PYCXX_LIB)/pycxx$(SAN).o         \
	$(SRC)/app$(SAN).o                 \
	$(SRC)/disk$(SAN).o                \
	$(SRC)/hash$(SAN).o                \
	$(SRC)/lib$(SAN).o                 \
	$(SRC)/non_portable.o              \
	$(SRC)/pycxx$(SAN).o               \
	$(SRC)/rpc_job$(SAN).o             \
	$(SRC)/time$(SAN).o                \
	$(SRC)/trace$(SAN).o               \
	$(SRC)/utils$(SAN).o               \
	$(SRC)/autodep/gather_deps$(SAN).o \
	$(SRC)/autodep/ptrace$(SAN).o      \
	$(SRC)/autodep/record$(SAN).o      \
	$(SRC)/job_exec$(SAN).o
	mkdir -p $(@D)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ -L$(PYTHON_LIB_DIR) -l$(PYTHON_LIB_BASE) $(LIB_SECCOMP) $(LINK_LIB)

$(BIN)/lmake : \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
	$(SRC)/lmake$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lshow : \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
	$(SRC)/lshow$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lforget : \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
	$(SRC)/lforget$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lfreeze : \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
	$(SRC)/lfreeze$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/xxhsum : \
	$(SRC)/disk$(SAN).o   \
	$(SRC)/hash$(SAN).o   \
	$(SRC)/lib$(SAN).o    \
	$(SRC)/non_portable.o \
	$(SRC)/time$(SAN).o   \
	$(SRC)/utils$(SAN).o  \
	$(SRC)/xxhsum$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

#
# job_exec
#

# ldepend generates error when -fsanitize=thread, but is mono-thread, so we don't care
$(BIN)/ldepend : \
	$(SRC)/app$(ASAN).o                     \
	$(SRC)/disk$(ASAN).o                    \
	$(SRC)/hash$(ASAN).o                    \
	$(SRC)/lib$(ASAN).o                     \
	$(SRC)/non_portable.o                   \
	$(SRC)/rpc_job$(ASAN).o                 \
	$(SRC)/time$(ASAN).o                    \
	$(SRC)/trace$(ASAN).o                   \
	$(SRC)/utils$(ASAN).o                   \
	$(SRC)/autodep/autodep_support$(ASAN).o \
	$(SRC)/autodep/record$(ASAN).o          \
	$(SRC)/autodep/ldepend$(ASAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(ASAN_FLAGS) -o $@ $^ $(LINK_LIB)
$(BIN)/lunlink           \
$(BIN)/ltarget           \
$(BIN)/lcritical_barrier \
$(BIN)/lcheck_deps       \
$(BIN)/ldep_crcs         \
: $(BIN)/ldepend
	rm -f $@
	ln $< $@

$(BIN)/autodep : \
	$(SRC)/app$(SAN).o                 \
	$(SRC)/disk$(SAN).o                \
	$(SRC)/hash$(SAN).o                \
	$(SRC)/lib$(SAN).o                 \
	$(SRC)/non_portable.o              \
	$(SRC)/rpc_job$(SAN).o             \
	$(SRC)/time$(SAN).o                \
	$(SRC)/trace$(SAN).o               \
	$(SRC)/utils$(SAN).o               \
	$(SRC)/autodep/gather_deps$(SAN).o \
	$(SRC)/autodep/ptrace$(SAN).o      \
	$(SRC)/autodep/record$(SAN).o      \
	$(SRC)/autodep/autodep$(SAN).o
	mkdir -p $(@D)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LIB_SECCOMP) $(LINK_LIB)

$(SRC)/autodep/autodep_ld_preload.o : $(SRC)/autodep/autodep_ld.cc
$(SLIB)/autodep_ld_preload.so : \
	$(SRC)/disk.o           \
	$(SRC)/hash.o           \
	$(SRC)/lib.o            \
	$(SRC)/non_portable.o   \
	$(SRC)/rpc_job.o        \
	$(SRC)/time.o           \
	$(SRC)/utils.o          \
	$(SRC)/autodep/record.o \
	$(SRC)/autodep/autodep_ld_preload.o
	mkdir -p $(@D)
	$(LINK_SO) -o $@ $^ $(LIB_SECCOMP) $(LINK_LIB)

$(SRC)/autodep/autodep_ld_audit.o : $(SRC)/autodep/autodep_ld.cc
$(SLIB)/autodep_ld_audit.so : \
	$(SRC)/disk.o               \
	$(SRC)/hash.o               \
	$(SRC)/lib.o                \
	$(SRC)/non_portable.o       \
	$(SRC)/rpc_job.o            \
	$(SRC)/time.o               \
	$(SRC)/utils.o              \
	$(SRC)/autodep/record.o     \
	$(SRC)/autodep/autodep_ld_audit.o
	mkdir -p $(@D)
	$(LINK_SO) -o $@ $^ $(LIB_SECCOMP) $(LINK_LIB)

$(LIB)/clmake.so : \
	$(SRC)/disk.o                    \
	$(SRC)/hash.o                    \
	$(SRC)/lib.o                     \
	$(SRC)/non_portable.o            \
	$(SRC)/rpc_job.o                 \
	$(SRC)/time.o                    \
	$(SRC)/utils.o                   \
	$(SRC)/autodep/autodep_support.o \
	$(SRC)/autodep/record.o          \
	$(SRC)/autodep/clmake.o
	mkdir -p $(@D)
	$(LINK_SO) -o $@ $^ $(LINK_LIB)

#
# Manifest
#
Manifest : .git/index
	git ls-files >$@
Manifest.stamp : Manifest
	>$@
include Manifest.stamp                                                         # force make to remake Manifest without reading it

#
# Unit tests
#

UT_DIR      := unit_tests
UT_BASE_DIR := $(UT_DIR)/base

UNIT_TESTS : Manifest \
	$(patsubst %.script,%.tok, $(shell grep -x '$(UT_DIR)/.*\.script' Manifest) ) \
	$(patsubst %.py,%.tok,     $(shell grep -x '$(UT_DIR)/[^/]*\.py'  Manifest) )

%.tok : %.script $(LMAKE_FILES) $(UT_BASE) Manifest
	mkdir -p $(@D)
	rm -rf $*.dir/*                                                            # keep $*.dir to ease debugging when we have cd inside and rerun make
	mkdir -p $*.dir
	for f in $$(grep '^$(UT_DIR)/base/' Manifest) ; do df=$*.dir/$${f#$(UT_DIR)/base/} ; mkdir -p $$(dirname $$df) ; cp $$f $$df ; done
	cd $*.dir ; find . -type f -printf '%P\n' > Manifest
	( cd $*.dir ; PATH=$(ROOT)/bin:$(ROOT)/_bin:$$PATH $(ROOT)/$< > ../$(@F) ) || ( cat $@ ; rm $@ ; exit 1 )

%.tok : %.py $(LMAKE_FILES) _lib/ut.py
	rm -rf $*.dir/*                                                            # keep $*.dir to ease debugging when we have cd inside and rerun make
	mkdir -p $*.dir
	cp $< $*.dir/Lmakefile.py
	( cd $*.dir ; PATH=$(ROOT)/bin:$(ROOT)/_bin:$$PATH PYTHONPATH=$(ROOT)/lib:$(ROOT)/_lib $(PYTHON) Lmakefile.py ) > $@ || ( cat $@ ; rm $@ ; exit 1 )

#
# lmake env
#

#
# lmake under lmake
#

LMAKE_ENV  : $(LMAKE_ENV)/stamp
LMAKE_TEST : $(LMAKE_ENV)/tok

$(LMAKE_ENV)/Manifest : Manifest
	@mkdir -p $(@D)
	grep -e ^_bin/ -e ^_lib/ -e ^doc/ -e ^ext/ -e ^lib/ -e ^src/ -e ^sys_config\$$ Manifest > $@
	grep ^$(@D)/ Manifest | sed s:$(@D)/::                                                  >>$@
	echo $(@F)                                                                              >>$@
$(LMAKE_ENV)/% : %
	@mkdir -p $(@D)
	cp $< $@
$(LMAKE_ENV)/stamp : $(LMAKE_FILES) $(LMAKE_ENV)/Manifest $(patsubst %,$(LMAKE_ENV)/%,$(shell grep -e ^_bin/ -e ^_lib/ -e ^doc/ -e ^ext/ -e ^lib/ -e ^src/ -e ^sys_config\$$ Manifest))
	touch $@
$(LMAKE_ENV)/tok : $(LMAKE_ENV)/stamp $(shell grep ^$(@D)/ Manifest )
	set -e ; cd $(LMAKE_ENV) ; $(ROOT)/bin/lmake lmake.tar.gz & sleep 1 ; $(ROOT)/bin/lmake lmake.tar.gz >$(@F) ; wait $$! ; touch $(@F)