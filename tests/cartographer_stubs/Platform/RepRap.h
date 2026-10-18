#pragma once
#include "Platform.h"
struct FakeRepRap { FakePlatform platform; FakePlatform& GetPlatform() { return platform; } };
inline FakeRepRap reprap;
