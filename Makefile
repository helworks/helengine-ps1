TARGET = helengine_ps1
TYPE = ps-exe
BINDIR ?= out/
BUILD ?= Release

CXXFLAGS += -std=c++20
CPPFLAGS += -Isrc
EXTRA_LDSCRIPT = linker/retail-memory.ld
DEMO_MODE ?= Clear
CORE_BOOT ?= 0
AUTHORED ?= 0
SCENE_MODULES ?= 0
ifeq ($(SCENE_MODULES),1)
# Keep the executable output local to this makefile. A command-line BINDIR would
# propagate into the SDK's recursive make and relocate its canonical libpsyqo.a.
BINDIR := scene-modules/build-out/
endif
CPU_PROFILE ?= 0
ifeq ($(CPU_PROFILE),1)
ifneq ($(AUTHORED),1)
$(error CPU_PROFILE requires authored build)
endif
ifneq ($(BUILD),Release)
$(error CPU_PROFILE requires Release build)
endif
endif
# Authored mode links generated Core like the boot checkpoint and adds the scene catalog,
# the embedded content archive, and the Core-driven scene host.
ifeq ($(AUTHORED),1)
CORE_BOOT := 1
endif
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
# Retain SDK abort messages in platform RAM without modifying the pinned SDK checkout.
LDFLAGS += -Wl,--wrap=_ZN5psyqo6Kernel8Internal5abortEPKcSt15source_location
LDFLAGS += -Wl,--wrap=_ZN5psyqo6Kernel8Internal5abortEv
# The SDK's scalar/array new operators return nullptr on exhaustion; generated C++ requires fatal failure.
LDFLAGS += -Wl,--wrap=_Znwj -Wl,--wrap=_Znaj
ifeq ($(wildcard generated-core/helengine_core_unity.cpp),)
$(error Core boot requires the final generated-core/helengine_core_unity.cpp)
endif
DEFINES += PS1_CORE_BOOT=1
CPPFLAGS += -Igenerated-core -Isrc/platform/ps1
CPPFLAGS_$(BUILD) += -msoft-float
ifeq ($(SCENE_MODULES),1)
include scene-modules/resident-sources.mk
PS1_RESIDENT_CORE_SRCS := $(filter-out generated-core/runtime/runtime_startup_manifest.cpp generated-core/ps1_runtime_scene_catalog.cpp,$(PS1_RESIDENT_CORE_SRCS))
SRCS += $(PS1_RESIDENT_CORE_SRCS)
DEFINES += PS1_SCENE_MODULES=1
CPPFLAGS += -Iscene-modules/metadata
LDFLAGS += $(PS1_MODULE_IMPORT_FLAGS)
CXXFLAGS += -mlong-calls -fno-use-cxa-atexit
else
SRCS += generated-core/helengine_core_unity.cpp
endif
SRCS += src/platform/ps1/Ps1RuntimeProvider.cpp \
        src/platform/ps1/modules/Ps1CodeModuleLifetime.cpp
# Every resident object, including legacy builds, has an empty ownership ID.
DEFINES += HELENGINE_CODE_MODULE_ID=\"\"
ifeq ($(AUTHORED),1)
ifeq ($(wildcard generated-core/ps1_runtime_scene_catalog.cpp),)
$(error Authored mode requires generated-core/ps1_runtime_scene_catalog.cpp from the PS1 platform builder)
endif
ifeq ($(wildcard generated-core/runtime/runtime_startup_manifest.cpp),)
$(error Authored mode requires the shared runtime loading policy manifest)
endif
ifeq ($(wildcard generated-core/runtime/runtime_startup_manifest.hpp),)
$(error Authored mode requires the shared runtime loading policy manifest header)
endif
DEFINES += PS1_AUTHORED_SCENE=1
AUTHORED_CORE_SRCS = src/platform/ps1/Ps1AuthoredScene.cpp \
                     src/platform/ps1/Ps1ContentStreamSource.cpp \
                     src/platform/ps1/Ps1SceneBootstrap.cpp \
                     src/platform/ps1/Ps1InputBackend.cpp \
                     src/platform/ps1/Ps1RenderManager2D.cpp \
                     src/platform/ps1/Ps1RenderManager3D.cpp \
                     src/platform/ps1/rendering/Ps1RuntimeModel.cpp \
                     src/platform/ps1/rendering/Ps1RuntimeMaterial.cpp \
                     generated-core/ps1_runtime_scene_catalog.cpp \
                     generated-core/runtime/runtime_startup_manifest.cpp
ifeq ($(CPU_PROFILE),1)
DEFINES += PS1_CPU_PROFILE=1
AUTHORED_CORE_SRCS += src/platform/ps1/profiling/Ps1CpuProfileSink.cpp src/platform/ps1/profiling/Ps1CpuProfileWindow.cpp src/platform/ps1/profiling/Ps1NumericPolicySelfTest.cpp
endif
SRCS += $(AUTHORED_CORE_SRCS)
ifeq ($(SCENE_MODULES),1)
SRCS += src/platform/ps1/modules/Ps1CodeModuleProvider.cpp \
        src/platform/ps1/modules/Ps1DiscContent.cpp \
        src/platform/ps1/modules/Ps1SceneModuleMetadata.cpp \
        scene-modules/metadata/ps1_scene_module_metadata.cpp
else
SRCS += authored/authored_assets.s
endif
CORE_RTTI_OBJS = $(AUTHORED_CORE_SRCS:.cpp=.o)
else
SRCS += src/platform/ps1/Ps1CoreBoot.cpp
CORE_RTTI_OBJS = src/platform/ps1/Ps1CoreBoot.o
endif
FDLIBM_ROOT = third_party/nugget/third_party/uC-sdk/libm/src
FDLIBM_NAMES = s_atan s_scalbn s_copysign \
               e_acos e_asin e_atan2 e_sqrt e_log e_fmod e_rem_pio2 \
               k_rem_pio2 k_sin k_cos k_tan
FDLIBM_SRCS = $(addprefix $(FDLIBM_ROOT)/,$(addsuffix .c,$(FDLIBM_NAMES)))
SRCS += $(FDLIBM_SRCS)
# Real's binary32 math uses genuine float kernels, independent of authored double math.
FLOAT_FDLIBM_SRCS = $(wildcard third_party/newlib-float/*.c)
SRCS += $(FLOAT_FDLIBM_SRCS)
$(FLOAT_FDLIBM_SRCS:.c=.o) $(FLOAT_FDLIBM_SRCS:.c=.dep): CFLAGS += -fsingle-precision-constant -fno-builtin -ffp-contract=off

$(FDLIBM_SRCS:.c=.o) $(FDLIBM_SRCS:.c=.dep): CFLAGS += -D__LITTLE_ENDIAN -D_IEEE_LIBM
endif

include third_party/nugget/psyqo/psyqo.mk

ifeq ($(SCENE_MODULES),1)
# INSERT scripts precede the primary script; nooverlay must set the heap base first.
LDSCRIPTS = $(OVERLAYSCRIPT) scene-modules/arenas.ld $(LDSCRIPT) $(EXTRA_LDSCRIPT)
endif

ifeq ($(AUTHORED),1)
# The packer validates the declared list and emits the archive plus the incbin stub that
# exposes he_ps1_authored_archive/_end to the content stream source.
authored/authored_assets.s authored/authored_assets.bin: authored-declarations.json scripts/embed-authored-assets.py
	mkdir -p authored
	python3 -B scripts/embed-authored-assets.py --declarations authored-declarations.json \
	    --output-archive authored/authored_assets.bin --output-assembly authored/authored_assets.s \
	    --incbin-path authored/authored_assets.bin
authored/authored_assets.o: authored/authored_assets.bin
endif

ifeq ($(CORE_BOOT),1)
generated-core/helengine_core_unity.o $(CORE_RTTI_OBJS): CXXFLAGS += -frtti
ifeq ($(SCENE_MODULES),1)
$(PS1_RESIDENT_CORE_SRCS:.cpp=.o) src/platform/ps1/modules/Ps1CodeModuleProvider.o src/platform/ps1/modules/Ps1DiscContent.o src/platform/ps1/modules/Ps1SceneModuleMetadata.o scene-modules/metadata/ps1_scene_module_metadata.o: CXXFLAGS += -frtti
endif
ifeq ($(AUTHORED),1)
ifeq ($(BUILD),Release)
# The authored frame path uses measured throughput optimization instead of the SDK size default.
generated-core/helengine_core_unity.o: CPPFLAGS += -O3
ifeq ($(SCENE_MODULES),1)
# These hot Core units replace the unity object in the resident module build.
# Preserve its throughput optimization without expanding every resident unit.
PS1_RUNTIME_HOT_SRCS := $(filter generated-core/ViewportComponent.cpp generated-core/Entity.cpp generated-core/ViewportLayoutSnapshot.cpp generated-core/AuthoredLayoutScaleUtils.cpp,$(PS1_RESIDENT_CORE_SRCS))
$(PS1_RUNTIME_HOT_SRCS:.cpp=.o): CPPFLAGS += -O3
endif
src/platform/ps1/Ps1RenderManager2D.o src/platform/ps1/Ps1RenderManager3D.o: CPPFLAGS += -O3
src/platform/ps1/rendering/Ps1RasterRenderer.o src/platform/ps1/rendering/Ps1FrameContext.o: CPPFLAGS += -O3
endif
endif
# libsupc++ must follow libpsyqo so operator new/delete resolve to the PSYQo allocator.
# If libsupc++ came first, its throwing operator new would drag in the exception
# allocator, terminate handlers, personality routine and unwinder, none of which
# link against this freestanding SDK.
LIBRARIES := $(LIBRARIES) $(shell $(CXX) -msoft-float -print-file-name=libsupc++.a) $(shell $(CC) -msoft-float -print-libgcc-file-name)
endif
