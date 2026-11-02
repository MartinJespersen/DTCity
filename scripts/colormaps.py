from pathlib import Path
import struct

import matplotlib as mpl

file_name = Path(__file__).resolve().parents[1] / "src/city/generated/colormaps.h"
colormap = mpl.colormaps["RdYlGn"]
color_count = 256


with open(file_name, "w") as f:
    f.write("#pragma once\n\n")
    colors = [component for index in range(color_count)
              for component in colormap(index / (color_count - 1))[:3]]
    # The GPU buffer contains tightly packed, little-endian RGB float32 triples.
    byte_data = struct.pack(f"<{len(colors)}f", *colors)
    f.write("const U8 g_red_yellow_green_colormap[] = {\n")
    for i in range(0, len(byte_data), 32):
        chunk = byte_data[i : i + 32]
        hex_chunk = ", ".join(f"0x{byte:02x}" for byte in chunk)
        suffix = "," if i + 32 < len(byte_data) else ""
        f.write(f"    {hex_chunk}{suffix}\n")
    f.write("};\n")

print(f"Written {color_count} RdYlGn RGB colors ({len(byte_data)} bytes) to {file_name}")
