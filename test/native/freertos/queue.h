// Shim: production headers include <freertos/*.h>; native tests resolve them
// here via -I. and get the canonical NativePlatform stubs instead.
#pragma once
#include "platform/NativePlatform.h"
