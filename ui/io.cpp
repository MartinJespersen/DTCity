// check for mouse inside context area of window
// if (glfwGetWindowAttrib(window, GLFW_HOVERED))
// {
//     highlight_interface();
// }

// scroll call back
// glfwSetScrollCallback(window, scroll_callback);
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

// mouse click
// int state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
// if (state == GLFW_PRESS)
// {
// }
internal void
IO_InputStateUpdate(IO* io)
{
    io->scroll_x = 0.0;
    io->scroll_y = 0.0;
    glfwPollEvents();
    // zero out scroll values

    glfwGetCursorPos(io->window, &io->mousePosition.x, &io->mousePosition.y);
    io->leftClicked = glfwGetMouseButton(io->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}
