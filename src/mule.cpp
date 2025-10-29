#include "base/base_inc.hpp"
#include "os_core/os_core_inc.hpp"

#include "base/base_inc.cpp"
#include "os_core/os_core_inc.cpp"

int
LineIntersect(F64 x1, F64 y1, F64 x2, F64 y2, F64 x3, F64 y3, F64 x4, F64 y4, F64* x, F64* y)
{
    F64 EPS = 0.001;
    F64 mua, mub;
    F64 denom, numera, numerb;

    denom = (y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1);
    numera = (x4 - x3) * (y1 - y3) - (y4 - y3) * (x1 - x3);
    numerb = (x2 - x1) * (y1 - y3) - (y2 - y1) * (x1 - x3);

    /* Are the line coincident? */
    if (AbsF64(numera) < EPS && AbsF64(numerb) < EPS && AbsF64(denom) < EPS)
    {
        *x = (x1 + x2) / 2;
        *y = (y1 + y2) / 2;
        return (TRUE);
    }

    /* Are the line parallel */
    if (AbsF64(denom) < EPS)
    {
        *x = 0;
        *y = 0;
        return (false);
    }

    /* Is the intersection along the the segments */
    mua = numera / denom;
    mub = numerb / denom;
    if (mua < 0 || mua > 1 || mub < 0 || mub > 1)
    {
        *x = 0;
        *y = 0;
        return (false);
    }
    *x = x1 + mua * (x2 - x1);
    *y = y1 + mua * (y2 - y1);
    return (TRUE);
}

void
App()
{
    Vec2F64 v1 = {0.0, 0.0};
    Vec2F64 v2 = {0.0, 3.0};
    Vec2F64 v3 = {0.0, 3.0};
    Vec2F64 v4 = {3.0, 3.0};

    F64 x, y;
    LineIntersect(v1.x, v1.y, v2.x, v2.y, v3.x, v3.y, v4.x, v4.y, &x, &y);
    printf("Intersection point: (%f, %f)\n", x, y);
}
