TARGET = helengine_ps1
TYPE = ps-exe
BINDIR ?= out/

CXXFLAGS += -std=c++20
CPPFLAGS += -Isrc
EXTRA_LDSCRIPT = linker/retail-memory.ld
DEMO_MODE ?= Clear
DEFINES += PS1_DEMO_MODE_$(DEMO_MODE)

SRCS = src/main.cpp \
       src/platform/ps1/Ps1BootHost.cpp \
       src/platform/ps1/Ps1CubeScene.cpp \
       src/platform/ps1/Ps1Video.cpp

include third_party/nugget/psyqo/psyqo.mk
