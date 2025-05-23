#pragma once

#include <crescent/types.h>

struct font_data {
	u8 width, height;
	const u8* font_data;
};

extern const struct font_data g_font_8x16;
