ifdef GASNET_INTEL
TOOLCHAIN=-intel
$(info Use intel toolchain)
endif

ifdef GASNET_GNU
TOOLCHAIN=-gnu
$(info Use gnu toolchain)
endif

ifdef GASNET_CRAY
TOOLCHAIN=-cray
$(info Use cray toolchain)
endif

ifdef GASNET_INTEL
ifdef GASNET_GNU
$(error Only one toolchain can be active! Define either GASNET_GNU or GASNET_INTEL or GASNET_CRAY)
endif
ifdef GASNET_CRAY
$(error Only one toolchain can be active! Define either GASNET_GNU or GASNET_INTEL ir GASNET_CRAY)
endif
endif

ifdef GASNET_GNU
ifdef GASNET_CRAY
$(error Only one toolchain can be active! Define either GASNET_GNU or GASNET_INTEL ir GASNET_CRAY)
endif
endif 
