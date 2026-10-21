#pragma once
#include "AndroidBrandConfig.h"

#if ANDROID_IS_NEWBIE
#  include "NewbieUI.h"
namespace ActiveUI = NewbieUI;
#else
#  include "CyloneUI.h"
namespace ActiveUI = Cylone;
#endif
