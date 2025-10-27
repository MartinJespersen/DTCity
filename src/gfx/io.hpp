#pragma once
struct Context;
struct IO
{
    Arena* arena;

    Vec2F64 mouse_pos_cur;
    Vec2S64 mouse_pos_cur_s64;
    B32 mouse_left_clicked;
    F32 mouse_sensitivity;
    B32 is_cursor_inside_win;

    S32 framebuffer_width;
    S32 framebuffer_height;

    F64 scroll_x;
    F64 scroll_y;
    B32 w_btn_clicked;
    B32 s_btn_clicked;
    B32 a_btn_clicked;
    B32 d_btn_clicked;

    Vec2S32 window_size;
    B32 is_window_focused;

    // GLFW types
    GLFWwindow* window;
    B32 framebuffer_resized;
};

static void
io_input_state_update(IO* io);
static Vec2S32
io_wait_for_valid_framebuffer_size(IO* io_ctx);
static void
io_framebuffer_resize_callback(GLFWwindow* window, int width, int height);
static IO*
io_window_create(U32 window_width, U32 window_height);
static void
io_window_destroy(IO* io_ctx);
static void
io_scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
static void
io_new_frame(IO* io_ctx);
