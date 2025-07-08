namespace wrapper
{
struct Road
{
    VkBuffer inst_buffer;
    VkDeviceMemory inst_memory_buffer;
    VkDeviceSize inst_buffer_size;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory_buffer;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
};
} // namespace wrapper
