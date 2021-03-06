
#include "dosboxxr/lib/util/rgbinfo.h"
#include "dosboxxr/lib/util/rgb_bitmap_info.h"

template <class T> void stretchblt_bilinear_mmx(const rgb_bitmap_info &dbmp,const rgb_bitmap_info &sbmp);
void stretchblt_bilinear_mmx(const rgb_bitmap_info &dbmp,const rgb_bitmap_info &sbmp);
bool stretchblt_bilinear_mmx_can_do(const rgb_bitmap_info &dbmp,const rgb_bitmap_info &sbmp);

