# ============================================================================
#  SawitMod (iOS) — Theos 构建脚本
# ----------------------------------------------------------------------------
#  产物：一个注入式 dylib（.theos/obj/SawitMod.dylib），塞进 iOS 版游戏的
#  .app 里即可。不依赖 CydiaSubstrate/ElleKit —— hook 用 Dobby，入口用
#  __attribute__((constructor))。
#
#  依赖：
#    * Theos（https://theos.dev）与 iOS SDK（Xcode）
#    * Dobby 的 iOS arm64 静态库：SawitMod/ThirdParty/Dobby/libdobby.a
#      （编译方法见 docs/构建与注入.md）
#
#  构建：  make clean && make
#  调试：  make DEBUG=1
# ============================================================================

export TARGET = iphone:clang:latest:13.0
export ARCHS = arm64 arm64e

include $(THEOS)/makefiles/common.mk

LIBRARY_NAME = SawitMod

SAWIT_DIR := $(THEOS_PROJECT_DIR)/SawitMod

# 所有 C/C++/ObjC 源文件（tests/ 下的自检程序不参与构建）
SawitMod_FILES = $(shell find $(SAWIT_DIR) -type f \( -name '*.cpp' -o -name '*.mm' -o -name '*.m' -o -name '*.c' \) ! -path '*/tests/*')

# ---- 构建开关（与 Android.mk 对齐）----
SAWIT_OFFLINE_MODE ?= 1
SAWIT_LOCAL_FEATURES ?= $(SAWIT_OFFLINE_MODE)
SAWIT_LOCAL_PAYLOAD_ONLY ?= $(SAWIT_OFFLINE_MODE)

SAWIT_DEFINES = \
	-DSAWIT_OFFLINE_MODE=$(SAWIT_OFFLINE_MODE) \
	-DSAWIT_LOCAL_FEATURES=$(SAWIT_LOCAL_FEATURES) \
	-DSAWIT_LOCAL_PAYLOAD_ONLY=$(SAWIT_LOCAL_PAYLOAD_ONLY) \
	-DSAWIT_DUMP_ENABLE=0 \
	-DIMGUI_IMPL_OPENGL_ES3=1 \
	-DGL_SILENCE_DEPRECATION=1 \
	-DSM_ENABLE_LOG=0

SAWIT_INCLUDES = \
	-I$(SAWIT_DIR) \
	-I$(SAWIT_DIR)/Includes \
	-I$(SAWIT_DIR)/ThirdParty \
	-I$(SAWIT_DIR)/ThirdParty/imgui

SAWIT_COMMON_FLAGS = $(SAWIT_DEFINES) $(SAWIT_INCLUDES) -w -fvisibility=hidden

SawitMod_CFLAGS = -fobjc-arc $(SAWIT_COMMON_FLAGS)
SawitMod_CCFLAGS = -std=c++17 -fexceptions $(SAWIT_COMMON_FLAGS)
SawitMod_OBJCFLAGS = $(SAWIT_COMMON_FLAGS)
SawitMod_OBJCCFLAGS = -std=c++17 -fexceptions $(SAWIT_COMMON_FLAGS)

SawitMod_FRAMEWORKS = UIKit Foundation Security QuartzCore OpenGLES CoreGraphics
SawitMod_LIBRARIES = z
SawitMod_LDFLAGS = \
	-Wl,-undefined,dynamic_lookup \
	-Wl,-not_for_dyld_shared_cache \
	-Wl,-dead_strip \
	$(SAWIT_DIR)/ThirdParty/Dobby/libdobby.a

include $(THEOS_MAKE_PATH)/library.mk

# Dobby 是必须的外部依赖：缺了直接给一句人话，而不是让链接器吐 400 行 undefined。
before-all::
	@test -f "$(SAWIT_DIR)/ThirdParty/Dobby/libdobby.a" || \
		(echo ""; \
		 echo "  [SawitMod] 缺少 Dobby 静态库："; \
		 echo "             $(SAWIT_DIR)/ThirdParty/Dobby/libdobby.a"; \
		 echo "             编译方法见 docs/构建与注入.md 的「Dobby」一节。"; \
		 echo ""; exit 1)
