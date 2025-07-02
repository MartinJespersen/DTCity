struct IO
{
    B32 handle_input;

    Vec2F64 mousePosition;
    B32 leftClicked;

    // GLFW types
    GLFWwindow* window;

    double scroll_x;
    double scroll_y;
};

void
IO_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

internal void
IO_InputStateUpdate(IO* io);
