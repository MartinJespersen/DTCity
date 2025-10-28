static void
io_framebuffer_resize_callback(GLFWwindow* window, int width, int height)
{
    (void)width;
    (void)height;

    io_IO* io_ctx = reinterpret_cast<io_IO*>(glfwGetWindowUserPointer(window));
    io_ctx->framebuffer_resized = 1;
}

static io_IO*
io_window_create(U32 window_width, U32 window_height)
{
    Arena* arena = ArenaAlloc();
    io_IO* io_ctx = PushStruct(arena, io_IO);
    io_ctx->arena = arena;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    io_ctx->window = glfwCreateWindow(window_width, window_height, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(io_ctx->window, io_ctx);
    glfwSetFramebufferSizeCallback(io_ctx->window, io_framebuffer_resize_callback);
    glfwSetScrollCallback(io_ctx->window, io_scroll_callback);

    return io_ctx;
}

static void
io_window_destroy(io_IO* io_ctx)
{
    glfwDestroyWindow(io_ctx->window);
    glfwTerminate();
    ArenaRelease(io_ctx->arena);
}

static void
io_scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    io_IO* io_ctx = (io_IO*)glfwGetWindowUserPointer(window);
    io_ctx->scroll_x = xoffset;
    io_ctx->scroll_y = yoffset;
}

// ~mgj: IMPORTANT:
// Never pass a reference to a glfw function if you expect it to be consistent value.
// I have run into an issue where the glfwGetCursorPos function resets the cursor position
// internally to a 0 value. This means that another thread may view the reset value before it is set
// to the correct value.

static void
io_input_state_update(io_IO* input)
{
    glfwPollEvents();

    S32 window_size_x;
    S32 window_size_y;
    glfwGetWindowSize(input->window, &window_size_x, &window_size_y);
    input->window_size.x = window_size_x ? window_size_x : input->window_size.x;
    input->window_size.y = window_size_y ? window_size_y : input->window_size.y;

    // Mouse updates
    F64 mouse_x;
    F64 mouse_y;
    glfwGetCursorPos(input->window, &mouse_x, &mouse_y);
    input->mouse_pos_cur.x = mouse_x;
    input->mouse_pos_cur.y = mouse_y;
    input->mouse_pos_cur_s64.x = floor(mouse_x);
    input->mouse_pos_cur_s64.y = floor(mouse_y);

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

    // framebuffer update
    S32 framebuffer_width = 0;
    S32 framebuffer_height = 0;
    glfwGetFramebufferSize(input->window, &framebuffer_width, &framebuffer_height);

    if (framebuffer_width > 0 && framebuffer_height > 0)
    {
        input->framebuffer_width = framebuffer_width;
        input->framebuffer_height = framebuffer_height;
    }
}

static Vec2S32
io_wait_for_valid_framebuffer_size(io_IO* io_ctx)
{
    // framebuffer update
    S32 framebuffer_width = 0;
    S32 framebuffer_height = 0;
    glfwGetFramebufferSize(io_ctx->window, &framebuffer_width, &framebuffer_height);
    while (framebuffer_width <= 0 || framebuffer_height <= 0)
    {
        io_ctx->framebuffer_width = framebuffer_width;
        io_ctx->framebuffer_height = framebuffer_height;
    }
    Vec2S32 framebuffer_dim = {framebuffer_width, framebuffer_height};
    return framebuffer_dim;
}

static void
io_new_frame(io_IO* io_ctx)
{
    io_ctx->scroll_x = 0.0;
    io_ctx->scroll_y = 0.0;
    ImGui_ImplGlfw_NewFrame();
}
