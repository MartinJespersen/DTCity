# Model fallback draws buffer

The compute fallback writes one indexed draw command for each instance of a model
primitive. Vulkan reads these commands from the same buffer when
`model_instance_rendering()` calls `vkCmdDrawIndexedIndirect()`.

The shader declares the buffer in
[model_lod.comp](../src/shaders/model_instance/model_lod.comp):

```glsl
layout(std430, set = 0, binding = 4) writeonly buffer Draws
{
    uint words[];
} draws;
```

`words` is a flat array of 32-bit unsigned integers. Every five words form one
`VkDrawIndexedIndirectCommand`: **20 bytes per command**, with no header or padding
between commands. The `writeonly` qualifier applies to shader access; Vulkan's
indirect drawing operation reads this buffer later.

## One command

Offsets in this table are relative to the start of a command. The macros are
shared by C++ and GLSL through [shader_shared.h](../src/shaders/shader_shared.h).

| Word offset | Byte offset | Macro | Vulkan field | Value written by compute |
|---|---|---|---|---|
| 0 | 0 | `MODEL_DRAW_INDEX_COUNT_OFFSET` | `indexCount` (`uint32_t`) | Selected LOD's index count. |
| 1 | 4 | `MODEL_DRAW_INSTANCE_COUNT_OFFSET` | `instanceCount` (`uint32_t`) | `1`: this command draws one instance. |
| 2 | 8 | `MODEL_DRAW_FIRST_INDEX_OFFSET` | `firstIndex` (`uint32_t`) | Selected LOD's first index in the bound index buffer. |
| 3 | 12 | `MODEL_DRAW_VERTEX_OFFSET` | `vertexOffset` (`int32_t`) | `0`: no offset is added to fetched vertex indices. |
| 4 | 16 | `MODEL_DRAW_FIRST_INSTANCE_OFFSET` | `firstInstance` (`uint32_t`) | Instance index within the batch, or `0` when indirect first-instance support is unavailable. |

`MODEL_DRAW_INDEXED_WORD_COUNT` is `5`. Although GLSL stores every field as a
`uint`, Vulkan interprets the fourth word as a signed integer. The current writer
stores zero in that field.

```text
draws.words[]                         byte offset

Command 0: words  0..4     ┌──────────────────────┐   0
                          │ indexCount           │
                          │ instanceCount        │
                          │ firstIndex           │
                          │ vertexOffset         │
                          │ firstInstance        │
Command 1: words  5..9     ├──────────────────────┤  20
                          │ same five fields     │
Command 2: words 10..14    ├──────────────────────┤  40
                          │ same five fields     │
Command 3: words 15..19    ├──────────────────────┤  60
                          │ same five fields     │
                          └──────────────────────┘  80
```

## Addressing commands and LOD data

A batch occupies a contiguous range of commands. `pc.draw_offset` is the batch's
starting **command index**, measured from the start of the draws buffer.

```glsl
uint instance = pc.first_instance + gl_GlobalInvocationID.x;
uint command = (pc.draw_offset + instance) * MODEL_DRAW_INDEXED_WORD_COUNT;
```

Here, `instance` is relative to the batch. During compute dispatch,
`pc.first_instance` is the starting instance of the current dispatch chunk. This
lets a large batch use several dispatches. Invocations with
`instance >= pc.instance_count` return without writing.

Compute selects the LOD, then reads its record from the geometry buffer at
binding 2:

```glsl
uint lod_entry = MODEL_GEOMETRY_LOD_TABLE_OFFSET + lod * MODEL_LOD_ENTRY_WORD_COUNT;

draws.words[command + MODEL_DRAW_INDEX_COUNT_OFFSET] =
    meshlets.words[lod_entry + MODEL_LOD_ELEMENT_COUNT_OFFSET];
draws.words[command + MODEL_DRAW_FIRST_INDEX_OFFSET] =
    meshlets.words[lod_entry + MODEL_LOD_DATA_OFFSET];
```

On the fallback path, that geometry buffer contains its metadata header followed
by ordinary `uint32_t` indices. It is bound as the index buffer at byte offset zero.
Therefore, `firstIndex` includes the header and any earlier LOD data. It is an
**index-element offset**, not a byte offset. Each index occupies four bytes.

The draws buffer contains commands only. Vertex data, index data, transforms,
textures, and pipeline state are supplied separately.

## Example: two batches

Assume batch A has two instances and batch B has three:

```text
Batch A: draw_offset = 0, instance_count = 2 → commands 0, 1
Batch B: draw_offset = 2, instance_count = 3 → commands 2, 3, 4
```

For instance `1` of batch B:

```text
command index = draw_offset + instance = 2 + 1 = 3
word offset   = 3 × 5  = 15
byte offset   = 3 × 20 = 60
```

Suppose its selected LOD has 36 indices starting at index element 120. With
indirect first-instance support, compute writes:

```text
word:          15          16             17           18            19
field:    indexCount  instanceCount   firstIndex  vertexOffset  firstInstance
value:        36           1             120           0             1
```

Vulkan reads 36 indices beginning at byte `120 × 4 = 480` of the geometry/index
buffer. The vertex shader uses `pc.transform_offset + gl_InstanceIndex` to select
the model transform. For this batch, the current C++ code sets `transform_offset`
to `2`, so this command uses transform `3`.

Without indirect first-instance support, word 19 is zero. The CPU issues each
command separately and sets `pc.first_instance = 1` for this draw. The vertex
shader then uses that push constant to select the same transform.

## Buffer lifetime and drawing

[_vulkan_model_compute_prepare()](../src/render/vulkan/pipelines.cpp) allocates or
reuses `model_indirect_buffer[current_frame]` with both storage-buffer and
indirect-buffer usage. Each frame slot has its own buffer. Batches whose resources
are not ready are skipped; only generated command ranges are submitted for drawing.
Unused buffer capacity does not need valid commands.

After compute writes the commands, a barrier makes those writes visible to the
indirect draw stage:

```text
COMPUTE_SHADER / SHADER_WRITE
                ↓
DRAW_INDIRECT / INDIRECT_COMMAND_READ
```

`model_instance_rendering()` binds each batch's geometry and descriptors, then uses:

```text
buffer = model_indirect_buffer[current_frame]
offset = (batch.draw_offset + first) × sizeof(VkDrawIndexedIndirectCommand)
stride = sizeof(VkDrawIndexedIndirectCommand) = 20 bytes
```

The CPU supplies the draw count. The buffer does not contain a command-count
header. When supported, several commands are submitted in one call, up to the
device's `maxDrawIndirectCount`. Otherwise, the renderer submits one command per
call. No CPU readback of the generated commands is needed.

## Related code

- [Compute writer](../src/shaders/model_instance/model_lod.comp)
- [Shared offsets and command stride](../src/shaders/shader_shared.h)
- [LOD selection](../src/shaders/model_instance/lod_select.glsl)
- [Geometry buffer packing](../src/render/vulkan/vulkan_if.cpp): `render_model_lod_upload()`
- [Buffer preparation and indirect drawing](../src/render/vulkan/pipelines.cpp): `_vulkan_model_compute_prepare()` and `model_instance_rendering()`
- [Vertex shader and transform selection](../src/shaders/model_instance/model.vert)
- [GPU command validation](../tests/render/test_model_lod_compute.cpp)
