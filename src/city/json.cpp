namespace city
{
g_internal void
simulator_coordinate_batch_append(Arena* arena, CoordinateBatch* batch, Buffer<Coordinate> incoming, B32 reset)
{
    // A reset supersedes earlier queued events; deltas retain every event in order.
    if (reset)
    {
        *batch = {incoming, true};
    }
    else if (incoming.size)
    {
        Buffer<Coordinate> merged = buffer_alloc<Coordinate>(arena, batch->coordinates.size + incoming.size);
        if (batch->coordinates.size)
        {
            MemoryCopy(merged.data, batch->coordinates.data, batch->coordinates.size * sizeof(Coordinate));
        }
        MemoryCopy(merged.data + batch->coordinates.size, incoming.data, incoming.size * sizeof(Coordinate));
        batch->coordinates = merged;
    }
}
} // namespace city
