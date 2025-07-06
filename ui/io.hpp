struct Context;
struct IO
{
    Vec2F64 mouse_pos_cur;
    B32 mouse_left_clicked;
    F32 mouse_sensitivity;
    B32 is_cursor_inside_win;

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
};

static void
IO_InputStateUpdate(IO* io);

static void
VK_FramebufferResizeCallback(GLFWwindow* window, int width, int height);

static void
InitWindow(Context* ctx);

void
IO_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

static void
IO_InputReset(IO* io);
