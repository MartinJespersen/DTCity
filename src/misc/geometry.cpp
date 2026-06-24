

glm::vec3
ui_direction_normal_from_euler_angles(F32 yaw, F32 pitch)
{
    glm::vec3 direction;
    direction.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.y = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    direction.z = sin(glm::radians(pitch));

    return glm::normalize(direction);
}
