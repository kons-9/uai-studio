#pragma once

#include <tk/tkernel.h>

struct TD_HDSP {
	FP exec = nullptr;
	FP stop = nullptr;
};

struct TD_HINT {
	FP enter = nullptr;
	FP leave = nullptr;
};

extern "C" {
ER td_hok_dsp(const TD_HDSP *hook);
ER td_hok_int(const TD_HINT *hook);
}
