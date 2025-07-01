// check for mouse inside context area of window
// if (glfwGetWindowAttrib(window, GLFW_HOVERED))
// {
//     highlight_interface();
// }

// scroll call back
// glfwSetScrollCallback(window, scroll_callback);
// void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
// {
// }

// mouse click
// int state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
// if (state == GLFW_PRESS)
// {
// }
internal void
IO_InputStateUpdate(IO* io)
{
    glfwPollEvents();

    glfwGetCursorPos(io->window, &io->mousePosition.x, &io->mousePosition.y);
    io->leftClicked = glfwGetMouseButton(io->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}
