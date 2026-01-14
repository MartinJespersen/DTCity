import array

import matplotlib as mpl

file_name = "colormaps.h"
colormaps = ["viridis", "plasma", "inferno", "magma"]


with open(file_name, "w") as f:
    for colormap in colormaps:
        color_list_flattened = [
            color
            for color_triplet in mpl.colormaps[colormap].colors
            for color in color_triplet
        ]
        float_array = array.array("f", color_list_flattened)
        byte_data = float_array.tobytes()
        f.write(f"read_only const U8 g_colormap_{colormap}[] = {{\n")
        for i in range(0, len(byte_data), 16):
            chunk = byte_data[i : i + 16]
            hex_chunk = ", ".join(f"0x{byte:02x}" for byte in chunk)
            f.write(f"    {hex_chunk},\n")
        f.write("};\n")

print(f"Written {len(byte_data)} bytes to colormap_{colormap}.h")
