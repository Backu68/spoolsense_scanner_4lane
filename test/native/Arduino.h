// Shim: production sources include <Arduino.h>; native tests resolve it here
// via -I. and get the canonical NativePlatform stubs instead.
#pragma once
#include "platform/NativePlatform.h"
