#pragma once
#include "render/render.hpp"

struct RoadTestRenderer
{
    U64 created;
    U64 destroyed;
    U64 submitted;
    U64 count_updates;
    U32 frame_index;
    bool loaded[32];
    bool released[32];
    render::ThreadWorkerCmdCtx commands;
};
