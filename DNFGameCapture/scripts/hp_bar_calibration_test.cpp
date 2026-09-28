#include "../HpBarCalibration.h"
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace dnf::hp;
static bool Equal(PixelRect a, PixelRect b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
int main() {
    assert(!HasFrame(0, 0)); assert(!HasFrame(19, 720));
    assert(ToPixel(std::numeric_limits<float>::quiet_NaN(), 1920) == 0);
    assert(ToPixel(-1, 1920) == 0); assert(ToPixel(2, 1920) == 1920);
    const float initial[4] = { .2f, .03f, .45f, .07f };
    for (const auto size : { PixelRect{0,0,800,600}, PixelRect{0,0,1280,720}, PixelRect{0,0,1920,1080}, PixelRect{0,0,3840,2160} }) {
        const int w = size.right, h = size.bottom;
        auto r = ToPixels(initial, w, h); assert(IsValid(r, w, h));
        const auto original = r;
        for (int i=0; i<100; ++i) {
            assert(Nudge(r,w,h,Handle::Whole,1,0));
            assert(Nudge(r,w,h,Handle::Whole,-1,0));
        }
        assert(Equal(r,original));
        assert(Nudge(r,w,h,Handle::Whole,10,10));
        assert(r.left == original.left+10 && r.right == original.right+10);
        assert(r.top == original.top+10 && r.bottom == original.bottom+10);
        float normalized[4]; assert(ToNormalized(r,w,h,normalized));
        assert(Equal(r,ToPixels(normalized,w,h)));
        // Match SaveHpBarRectsToIni precision: repeated saves must not lose a pixel.
        for (float& v : normalized) { char s[40]; std::snprintf(s,sizeof(s),"%.6f",v); v=static_cast<float>(std::atof(s)); }
        assert(Equal(r,ToPixels(normalized,w,h)));
        assert(Nudge(r,w,h,Handle::Whole,INT_MAX,INT_MAX));
        assert(r.right == w && r.bottom == h);
        assert(r.right-r.left == original.right-original.left);
        assert(Nudge(r,w,h,Handle::Whole,INT_MIN,INT_MIN));
        assert(r.left==0 && r.top==0);
        assert(Nudge(r,w,h,Handle::TopLeft,INT_MAX,INT_MAX));
        assert(r.right-r.left==MinWidth(w) && r.bottom-r.top==MinHeight(h));
        assert(Nudge(r,w,h,Handle::BottomRight,INT_MIN,INT_MIN));
        assert(IsValid(r,w,h));
        assert(Nudge(r,w,h,Handle::BottomRight,INT_MAX,INT_MAX));
        assert(r.right==w && r.bottom==h);
        PixelRect bad{20,10,10,40}; assert(!ToNormalized(bad,w,h,normalized));
        assert(!Nudge(bad,w,h,Handle::Whole,1,0));
        assert(!IsValid(PixelRect{-1,0,100,30},w,h));
        assert(!IsValid(PixelRect{0,0,w+1,h},w,h));
        assert(!IsValid(PixelRect{0,0,20,1},w,h));
        // Defaults/snapshot cancel are normalized-value copies, never inferred from preview size.
        float snapshot[4]; for(int k=0;k<4;++k) snapshot[k]=initial[k];
        assert(Equal(ToPixels(snapshot,w,h),original));
    }
    std::cout << "hp_bar_calibration: pixel rounding, original-image steps, bounds, minimum sizes, 4 resolutions and INI round trips passed\n";
}
