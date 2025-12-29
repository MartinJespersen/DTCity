#pragma once

static B32
ui_line_intersect(F64 x1, F64 y1, F64 x2, F64 y2, F64 x3, F64 y3, F64 x4, F64 y4, F64* x, F64* y);
static B32
ui_line_intersect_2f32(Vec2F32 v0, Vec2F32 v1, Vec2F32 v2, Vec2F32 v3, Vec2F32* res);
glm::vec3
ui_direction_normal_from_euler_angles(F32 yaw, F32 pitch);
