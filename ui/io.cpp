// check for mouse inside context area of window
// if (glfwGetWindowAttrib(window, GLFW_HOVERED))
// {
//     highlight_interface();
// }

static void
VK_FramebufferResizeCallback(GLFWwindow* window, int width, int height)
{
    (void)width;
    (void)height;

    auto context = reinterpret_cast<Context*>(glfwGetWindowUserPointer(window));
    context->vulkanContext->framebuffer_resized = 1;
}

static void
InitWindow(Context* ctx)
{
    IO* io_ctx = ctx->io;
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    io_ctx->window = glfwCreateWindow(800, 600, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(io_ctx->window, ctx);
    glfwSetFramebufferSizeCallback(io_ctx->window, VK_FramebufferResizeCallback);
    glfwSetScrollCallback(io_ctx->window, IO_ScrollCallback);
}

struct Context;
void
IO_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    Context* ctx = (Context*)glfwGetWindowUserPointer(window);
    IO* io_ctx = ctx->io;
    io_ctx->scroll_x = xoffset;
    io_ctx->scroll_y = yoffset;
    printf("Scroll callback called with xoffset: %f, yoffset: %f\n", xoffset, yoffset);
}

// IMPORTANT:
// Never pass a reference to a glfw function if you expect it to be consistent value.
// I have run into an issue where the glfwGetCursorPos function resets the cursor position
// internally to a 0 value. This means that another thread may view the reset value before it is set
// to the correct value.

static void
IO_InputStateUpdate(IO* input)
{
    glfwPollEvents();

    // Mouse updates
    S32 window_size_x;
    S32 window_size_y;
    glfwGetWindowSize(input->window, &window_size_x, &window_size_y);
    input->window_size.x = window_size_x;
    input->window_size.y = window_size_y;

    F64 mouse_x;
    F64 mouse_y;
    glfwGetCursorPos(input->window, &mouse_x, &mouse_y);
    input->mouse_pos_cur.x = mouse_x;
    input->mouse_pos_cur.y = mouse_y;

    input->mouse_left_clicked =
        glfwGetMouseButton(input->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    // Button updates
    input->w_btn_clicked = glfwGetKey(input->window, GLFW_KEY_W) == GLFW_PRESS;
    input->s_btn_clicked = glfwGetKey(input->window, GLFW_KEY_S) == GLFW_PRESS;
    input->a_btn_clicked = glfwGetKey(input->window, GLFW_KEY_A) == GLFW_PRESS;
    input->d_btn_clicked = glfwGetKey(input->window, GLFW_KEY_D) == GLFW_PRESS;

    input->is_cursor_inside_win =
        input->mouse_pos_cur.x >= 0 && input->mouse_pos_cur.x < input->window_size.x &&
        input->mouse_pos_cur.y >= 0 && input->mouse_pos_cur.y < input->window_size.y;
    input->is_window_focused = glfwGetWindowAttrib(input->window, GLFW_FOCUSED) == GLFW_TRUE;
}

static void
IO_InputReset(IO* io)
{
    io->scroll_x = 0.0;
    io->scroll_y = 0.0;
}
