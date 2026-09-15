TARGET = helengine_ps1
TYPE = ps-exe
BINDIR ?= out/
BUILD ?= Release

CXXFLAGS += -std=c++20
CPPFLAGS += -Isrc
EXTRA_LDSCRIPT = linker/retail-memory.ld
DEMO_MODE ?= Clear
CORE_BOOT ?= 0
DEFINES += PS1_DEMO_MODE_$(DEMO_MODE)
DEFINES += $(SDK_DEFINES)

SRCS = src/main.cpp \
       src/platform/ps1/Ps1BootHost.cpp \
       src/platform/ps1/Ps1CubeScene.cpp \
       src/platform/ps1/Ps1Video.cpp \
       src/platform/ps1/rendering/Ps1FrameContext.cpp \
       src/platform/ps1/rendering/Ps1CubeMesh.cpp \
       src/platform/ps1/rendering/Ps1RasterRenderer.cpp \
       src/platform/ps1/rendering/Ps1RotationClock.cpp

ifeq ($(CORE_BOOT),1)
ifeq ($(wildcard generated-core/helengine_core_unity.cpp),)
$(error Core boot requires the final generated-core/helengine_core_unity.cpp)
endif
DEFINES += PS1_CORE_BOOT=1
CPPFLAGS += -Igenerated-core -Isrc/platform/ps1
CPPFLAGS_$(BUILD) += -msoft-float
SRCS += generated-core/helengine_core_unity.cpp \
        src/platform/ps1/Ps1CoreBoot.cpp \
        src/platform/ps1/Ps1RuntimeProvider.cpp \
        src/platform/ps1/Ps1Math.cpp
FDLIBM_ROOT = third_party/nugget/third_party/uC-sdk/libm/src
FDLIBM_NAMES = s_sin s_cos s_tan s_atan s_ceil s_floor s_fabs s_scalbn s_finite s_copysign \
               e_acos e_asin e_atan2 e_sqrt e_log e_fmod e_rem_pio2 \
               w_acos w_asin w_atan2 w_sqrt w_log w_fmod \
               k_rem_pio2 k_sin k_cos k_tan
FDLIBM_SRCS = $(addprefix $(FDLIBM_ROOT)/,$(addsuffix .c,$(FDLIBM_NAMES)))
SRCS += $(FDLIBM_SRCS)
$(FDLIBM_SRCS:.c=.o) $(FDLIBM_SRCS:.c=.dep): CFLAGS += -D__LITTLE_ENDIAN -D_IEEE_LIBM
endif

include third_party/nugget/psyqo/psyqo.mk

ifeq ($(CORE_BOOT),1)
generated-core/helengine_core_unity.o src/platform/ps1/Ps1CoreBoot.o: CXXFLAGS += -frtti
# libsupc++ must follow libpsyqo so operator new/delete resolve to the PSYQo allocator.
# If libsupc++ came first, its throwing operator new would drag in the exception
# allocator, terminate handlers, personality routine and unwinder, none of which
# link against this freestanding SDK.
LIBRARIES := $(LIBRARIES) $(shell $(CXX) -msoft-float -print-file-name=libsupc++.a) $(shell $(CC) -msoft-float -print-libgcc-file-name)
endif


