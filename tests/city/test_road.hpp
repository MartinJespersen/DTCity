#pragma once
#include "render/render.hpp"

struct RoadTestRenderer
{
    U64 created;
    U64 destroyed;
    U64 submitted;
    bool loaded[32];
    bool released[32];
    render::ThreadWorkerCmdCtx commands;
};
