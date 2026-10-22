# Project Midas (GoldenEye 007) for Alpha Touch. Mirrors CMakeLists.txt's GE_VIDEO + GE_GLES build;
# every asset comes off the ROM, the three menu models included (no ROM-extracted files needed).

LOCAL_PATH := $(call my-dir)/..

MIDAS_PATH := $(LOCAL_PATH)

# The build stamp CMake makes on desktop
MIDAS_GEN := $(shell $(MIDAS_PATH)/mobile/generate.sh >&2 || echo failed)
ifneq ($(MIDAS_GEN),)
    $(error project_midas: mobile/generate.sh failed)
endif

MIDAS_DEFS := -DVERSION_US -DLANG_US -DREFRESH_NTSC -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0 \
              -DNONMATCHING -DAVOID_UB -D_LANGUAGE_C -DGE_VIDEO=1 -DGE_GLES=1
ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    MIDAS_DEFS += -DGE_64BIT=1
endif

# The game's own headers shadow libc's for C; C++ (fast3d, libc++) must find the system ones first
MIDAS_INC_DIRS := . include include/ultra64 include/PR src src/game src/inflate port/include src/libultra mobile/generated

# Touch/gamepad bridge, built apart so the shadowing headers stay out of it
include $(CLEAR_VARS)
LOCAL_PATH := $(MIDAS_PATH)
LOCAL_MODULE := project_midas_mobile
LOCAL_CFLAGS := -std=gnu11 -O2
LOCAL_C_INCLUDES := $(TOP_DIR)/SDL2_OpenTouch/SDL2-2.0.12/include $(TOP_DIR)/Clibs_OpenTouch
LOCAL_SRC_FILES := mobile/game_interface.c mobile/ge_lang.c mobile/bionic_compat.c
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_PATH := $(MIDAS_PATH)
LOCAL_MODULE := project_midas

# GE has its own sprintf, which bionic's fortified one would collide with
LOCAL_DISABLE_FORMAT_STRING_CHECKS := true
LOCAL_CFLAGS := $(MIDAS_DEFS) -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
LOCAL_CFLAGS += -D__MOBILE__ -DENGINE_NAME=\"goldeneye\" -DPERFECT_DARK
# ARM floats are IEEE single already; no FMA fusing, which rounds unlike the N64 FPU
LOCAL_CFLAGS += -ffp-contract=off -fno-strict-aliasing -fwrapv -funsigned-char -fno-omit-frame-pointer -g -O2
LOCAL_CFLAGS += -Wno-everything

# pointer<->int truncation and missing prototypes are errors on 64-bit, as in CMakeLists.txt
LOCAL_CONLYFLAGS := -std=gnu11 -ffreestanding -fms-extensions -fcommon \
                    -Werror=int-conversion -Werror=implicit-function-declaration \
                    -Werror=incompatible-function-pointer-types -Werror=pointer-to-int-cast \
                    -Werror=int-to-pointer-cast -Werror=void-pointer-to-int-cast -Werror=int-to-void-pointer-cast \
                    -Dsinf=portN64Sinf -Dcosf=portN64Cosf \
                    $(foreach d,$(MIDAS_INC_DIRS),-I$(MIDAS_PATH)/$(d))
LOCAL_CPPFLAGS := -std=c++20 $(foreach d,$(MIDAS_INC_DIRS),-idirafter $(MIDAS_PATH)/$(d))

# SDL2 itself only: SDL2_mixer's music.h would shadow the game's
LOCAL_C_INCLUDES := $(TOP_DIR)/SDL2_OpenTouch/SDL2-2.0.12/include \
                    $(TOP_DIR)/MobileTouchControls \
                    $(TOP_DIR)/Clibs_OpenTouch \
                    $(TOP_DIR)/Clibs_OpenTouch/alpha

# Clibs_OpenTouch sources
LOCAL_SRC_FILES := ../../Clibs_OpenTouch/alpha/android_jni.cpp \
                   mobile/touch_interface_ge.cpp \
                   ../../Clibs_OpenTouch/touch_interface_base.cpp

MIDAS_WILD = $(subst $(MIDAS_PATH)/,,$(wildcard $(addprefix $(MIDAS_PATH)/,$(1))))

# Game logic, and the engine layer minus the N64-only machinery (CMakeLists.txt ENGINE_EXCLUDE)
LOCAL_SRC_FILES += $(call MIDAS_WILD,src/game/*.c)
LOCAL_SRC_FILES += $(filter-out $(addprefix src/,$(addsuffix .c,boot tlb_manage tlb_hardwire rmon usb debug crash motor)),$(call MIDAS_WILD,src/*.c))

# gu math and the audio synthesizer (CMakeLists.txt GU_SRC)
LOCAL_SRC_FILES += $(addprefix src/libultra/gu/,align.c coss.c lookat.c lookatref.c mtxutil.c normalize.c ortho.c \
                     perspective.c rotate.c scale.c sins.c translate.c)
LOCAL_SRC_FILES += $(addprefix src/libultra/audio/,sl.c synthesizer.c event.c heapinit.c heapalloc.c cents2ratio.c copy.c \
                     synaddplayer.c synallocfx.c synallocvoice.c syndelete.c synfreevoice.c synsetfxmix.c synsetpan.c \
                     synsetpitch.c synsetpriority.c synsetvol.c synstartvoice.c synstartvoiceparam.c synstopvoice.c \
                     load.c save.c resample.c filter.c mainbus.c auxbus.c \
                     cseq.c csplayer.c cspgetstate.c cspplay.c cspsetseq.c cspsetvol.c cspstop.c seqpsetbank.c seqplayer.c seq.c)
LOCAL_SRC_FILES += $(addprefix src/libultrare/audio/,drvrNew.c reverb.c env.c)

# Port layer, video included
LOCAL_SRC_FILES += $(filter-out port/src/headless_stubs.c,$(call MIDAS_WILD,port/src/*.c))
LOCAL_SRC_FILES += src/libultrare/io/vitbl.c
LOCAL_SRC_FILES += assets/oddtextures.c assets/rarewarelogo.c assets/font_dl.c
LOCAL_SRC_FILES += port/fast3d/gfx_pc.cpp port/fast3d/gfx_cc.cpp port/fast3d/gfx_opengl.cpp port/fast3d/gfx_sdl2.cpp \
                   port/fast3d/glad/glad.c

LOCAL_LDLIBS := -lEGL -ldl -llog -lm -lGLESv3
LOCAL_LDFLAGS := -Wl,--version-script=$(MIDAS_PATH)/mobile/exports.map
LOCAL_STATIC_LIBRARIES := project_midas_mobile logwritter
LOCAL_SHARED_LIBRARIES := touchcontrols SDL2 saffal

include $(BUILD_SHARED_LIBRARY)
