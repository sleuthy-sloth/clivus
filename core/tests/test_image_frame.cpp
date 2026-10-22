// SPDX-License-Identifier: GPL-3.0-or-later
// Assert-style self-check for the pixel math; no test framework on purpose.
#include "clivus/image_frame.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int g_failures = 0;

void check(bool ok, const char* what, double got, double want)
{
    if (ok) return;
    std::printf("FAIL %s: got %.4f want %.4f\n", what, got, want);
    ++g_failures;
}
void eq(double got, double want, const char* what) { check(std::fabs(got - want) < 1e-6, what, got, want); }
}  // namespace

int main()
{
    using namespace clivus;

    // 1. rescale is applied, range follows the transformed values
    {
        const ImageFrame f = makeFrame({0.0f, 100.0f, 200.0f}, 3, 1, 0.5f, -10.0f);
        eq(f.pixels[0], -10.0, "rescale slope/intercept");
        eq(f.pixels[2], 90.0, "rescale slope/intercept");
        eq(f.minValue, -10.0, "minValue");
        eq(f.maxValue, 90.0, "maxValue");
    }

    // 2. padding never sets the range
    {
        const ImageFrame f = makeFrame({-2000.0f, 40.0f, 900.0f}, 3, 1, 1.0f, 0.0f, true, -2000.0f);
        eq(f.minValue, 40.0, "padding excluded from min");
        eq(f.maxValue, 900.0, "padding excluded from max");
    }

    // 3. all-padding image degrades to 0/0 instead of +inf
    {
        const ImageFrame f = makeFrame({-2000.0f, -2000.0f}, 2, 1, 1.0f, 0.0f, true, -2000.0f);
        eq(f.minValue, 0.0, "all-padding min");
        eq(f.maxValue, 0.0, "all-padding max");
    }

    // 4. linear VOI LUT: 0 -> 1, 50 -> 129, 100 -> 255, centre 50 / width 101
    {
        std::vector<float> stored(101);
        for (int i = 0; i <= 100; ++i) stored[size_t(i)] = float(i);
        const ImageFrame f = makeFrame(std::move(stored), 101, 1);
        std::vector<uint8_t> out(101);
        f.render(out.data(), 50.0, 101.0);
        eq(out[0], 1.0, "VOI just above window start");
        eq(out[50], 129.0, "VOI mid");
        eq(out[100], 255.0, "VOI above window end");
    }

    // 5. values below the window clamp to 0
    {
        std::vector<float> stored = {0.0f, 50.0f, 100.0f};
        const ImageFrame f = makeFrame(std::move(stored), 3, 1);
        std::vector<uint8_t> out(3);
        f.render(out.data(), 200.0, 10.0);
        eq(out[0], 0.0, "clamp below");
        eq(out[2], 0.0, "clamp below");
    }

    // 6. a zero window must not divide by zero
    {
        const ImageFrame f = makeFrame({5.0f, 6.0f}, 2, 1);
        std::vector<uint8_t> out(2);
        f.render(out.data(), 5.0, 0.0);
        check(out[0] <= 255, "zero window stays in range", out[0], 255);
    }

    // 7. full dynamic window
    {
        const ImageFrame f = makeFrame({-100.0f, 0.0f, 300.0f}, 3, 1);
        double c = 0, w = 0;
        f.fullWindow(c, w);
        eq(c, 100.0, "fullWindow centre");
        eq(w, 400.0, "fullWindow width");
    }

    // 8. invalid frame is inert
    {
        ImageFrame f;
        double c = 0, w = 0;
        f.fullWindow(c, w);
        eq(w, 1.0, "empty frame window");
        check(!f.valid(), "empty frame invalid", 0, 0);
    }

    if (g_failures == 0) std::printf("image_frame: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
