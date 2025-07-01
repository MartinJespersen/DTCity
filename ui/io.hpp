struct IO
{
    B32 handle_input;

    Vec2F64 mousePosition;
    B32 leftClicked;

    // GLFW types
    GLFWwindow* window;
};

internal void
IO_InputStateUpdate(IO* io);
