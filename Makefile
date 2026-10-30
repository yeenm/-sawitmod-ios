# ============================================================================
#  SawitMod (iOS) 鈥?Theos 鏋勫缓鑴氭湰
# ----------------------------------------------------------------------------
#  浜х墿锛氫竴涓敞鍏ュ紡 dylib锛?theos/obj/SawitMod.dylib锛夛紝濉炶繘 iOS 鐗堟父鎴忕殑
#  .app 閲屽嵆鍙€備笉渚濊禆 CydiaSubstrate/ElleKit 鈥斺€?hook 鐢?Dobby锛屽叆鍙ｇ敤
#  __attribute__((constructor))銆?#
#  渚濊禆锛?#    * Theos锛坔ttps://theos.dev锛変笌 iOS SDK锛圶code锛?#    * Dobby 鐨?iOS arm64 闈欐€佸簱锛歋awitMod/ThirdParty/Dobby/libdobby.a
#      锛堢紪璇戞柟娉曡 docs/鏋勫缓涓庢敞鍏?md锛?#
#  鏋勫缓锛? make clean && make
#  璋冭瘯锛? make DEBUG=1
# ============================================================================

export TARGET = iphone:clang:latest:13.0
export ARCHS = arm64 arm64e

include $(THEOS)/makefiles/common.mk

LIBRARY_NAME = SawitMod

SAWIT_DIR := $(THEOS_PROJECT_DIR)/SawitMod

# 鎵€鏈?C/C++/ObjC 婧愭枃浠讹紙tests/ 涓嬬殑鑷绋嬪簭涓嶅弬涓庢瀯寤猴級
SawitMod_FILES = $(shell find $(SAWIT_DIR) -type f \( -name '*.cpp' -o -name '*.mm' -o -name '*.m' -o -name '*.c' \) ! -path '*/tests/*')

# ---- 鏋勫缓寮€鍏筹紙涓?Android.mk 瀵归綈锛?---
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

# Dobby 鏄繀椤荤殑澶栭儴渚濊禆锛氱己浜嗙洿鎺ョ粰涓€鍙ヤ汉璇濓紝鑰屼笉鏄閾炬帴鍣ㄥ悙 400 琛?undefined銆?before-all::
	@test -f "$(SAWIT_DIR)/ThirdParty/Dobby/libdobby.a" || \
		(echo ""; \
		 echo "  [SawitMod] 缂哄皯 Dobby 闈欐€佸簱锛?; \
		 echo "             $(SAWIT_DIR)/ThirdParty/Dobby/libdobby.a"; \
		 echo "             缂栬瘧鏂规硶瑙?docs/鏋勫缓涓庢敞鍏?md 鐨勩€孌obby銆嶄竴鑺傘€?; \
		 echo ""; exit 1)
