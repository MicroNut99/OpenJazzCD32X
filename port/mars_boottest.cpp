/*
 * OpenJazz CD32X - J4a boot test: the C++ features OpenJazz relies on, checked on the 32X
 * before the game starts (port/plat_mars.c boot_test shows the results).
 */
#include <new>

namespace {
	volatile int ctor_mark;
	struct CtorCheck { CtorCheck() { ctor_mark = 0x0C70; } };
	CtorCheck ctor_check;  // a global object: its constructor must have run before main

	__attribute__((noinline)) int thrower(volatile int v)
	{
		if (v != 0) throw static_cast<int>(v);
		return -1;
	}
}

extern "C" int oj32x_test_ctor(void) { return ctor_mark == 0x0C70; }

extern "C" int oj32x_test_new(void)
{
	int *p = new (std::nothrow) int[256];
	if (!p) return 0;
	for (int i = 0; i < 256; ++i) p[i] = i;
	int ok = p[255] == 255;
	delete[] p;
	return ok;
}

/* 0x0123 = thrown and caught; anything else = the catch did not see the value */
extern "C" int oj32x_test_throw(void)
{
	try {
		return thrower(0x0123);
	} catch (int e) {
		return e;
	}
}
