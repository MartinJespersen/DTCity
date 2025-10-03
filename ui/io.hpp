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
IO_InputStateUpdate(IO* io);

static Vec2S32
IO_WaitForValidFramebufferSize(IO* io_ctx);
static void
VK_FramebufferResizeCallback(GLFWwindow* window, int width, int height);

static IO*
WindowCreate(U32 window_width, U32 window_height);
static void
WindowDestroy(IO* io_ctx);
static void
IO_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
static void
IO_NewFrame(IO* io_ctx);
