"""Compare compiled GLSL member offsets against the shared C++ types.

Run with --glslang PATH and --glm-include PATH when those dependencies are
provided by vcpkg. All generated sources and binaries stay in a temp directory.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--glslang', default='glslangValidator')
    parser.add_argument('--spirv-dis', default='spirv-dis')
    parser.add_argument('--cxx', default='clang++')
    parser.add_argument('--glm-include', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    shader_dir = root / 'src/shaders'
    header = (shader_dir / 'shader_shared.h').read_text()
    shared_types = set(re.findall(r'struct (?:(?:SHADER_ALIGN\(\d+\)|SHADER_ROOT) )?(\w+)\n', header))
    root_types = set(re.findall(r'struct SHADER_ROOT (\w+)\n', header))
    offsets = {}
    strides = {}
    member_strides = {}
    matrix_strides = {}
    alignments = {}
    shader_count = 0
    with tempfile.TemporaryDirectory(prefix='dtcity-shader-layouts-') as directory:
        temp = Path(directory)
        # Exercise every shared type, even if no current shader uses it. The
        # leading float exposes nested alignment; roots explicitly start at 16.
        fixture = ['#version 450', '#extension GL_GOOGLE_include_directive : require',
                   '#include "shader_shared.h"', 'layout(local_size_x = 1) in;']
        for binding, name in enumerate(sorted(shared_types)):
            alignment = 'layout(align = 16) ' if name in root_types else ''
            fixture.append(f'layout(std430, binding = {binding}) readonly buffer Test_{name} '
                           f'{{ float prefix; {alignment}{name} elements[]; }} probe_{name};')
        fixture.append('void main() {}')
        fixture_path = temp / 'all_layouts.comp'
        fixture_path.write_text('\n'.join(fixture) + '\n')
        for source in sorted(shader_dir.rglob('*')) + [fixture_path]:
            if source.suffix not in ('.vert', '.frag', '.comp', '.mesh', '.task'):
                continue
            text = source.read_text()
            if 'shader_shared.h' not in text and '"includes.glsl"' not in text:
                continue
            binary = temp / (source.name + '.spv')
            command = [args.glslang, '-V', '-I' + str(shader_dir), str(source), '-o', str(binary)]
            if source.suffix in ('.mesh', '.task') or source.name == 'tile_height.comp':
                command += ['--target-env', 'vulkan1.2']
            subprocess.run(command, check=True, capture_output=True, text=True)
            assembly = subprocess.check_output([args.spirv_dis, str(binary)], text=True)
            names = dict(re.findall(r'OpName (%\S+) "([^"]+)"', assembly))
            members = {(type_id, index): name for type_id, index, name in
                       re.findall(r'OpMemberName (%\S+) (\d+) "([^"]+)"', assembly)}
            for type_id, index, offset in re.findall(
                    r'OpMemberDecorate (%\S+) (\d+) Offset (\d+)', assembly):
                type_name = names.get(type_id)
                if type_name and type_name.startswith('Test_') and index == '1':
                    alignments[type_name.removeprefix('Test_')] = int(offset)
                if type_name not in shared_types:
                    continue
                key = (type_name, members[type_id, index])
                value = int(offset)
                assert offsets.setdefault(key, value) == value, (source, key, value)
            # Array strides must agree with C++ sizeof, including tail padding.
            arrays = dict(re.findall(r'(%\S+) = OpTypeRuntimeArray (%\S+)', assembly))
            array_strides = dict(re.findall(r'OpDecorate (%\S+) ArrayStride (\d+)', assembly))
            for array_id, stride in array_strides.items():
                type_name = names.get(arrays.get(array_id))
                if type_name in shared_types:
                    assert strides.setdefault(type_name, int(stride)) == int(stride)
            # Arrays inside structs also need matching element strides.
            for type_id, fields in re.findall(r'(%\S+) = OpTypeStruct ([^\n]+)', assembly):
                type_name = names.get(type_id)
                if type_name not in shared_types:
                    continue
                for index, field_type in enumerate(fields.split()):
                    if field_type in array_strides:
                        key = (type_name, members[type_id, str(index)])
                        value = int(array_strides[field_type])
                        assert member_strides.setdefault(key, value) == value, (source, key)
            for type_id, index, stride in re.findall(
                    r'OpMemberDecorate (%\S+) (\d+) MatrixStride (\d+)', assembly):
                type_name = names.get(type_id)
                if type_name in shared_types:
                    key = (type_name, members[type_id, index])
                    assert matrix_strides.setdefault(key, int(stride)) == int(stride), (source, key)
                    assert f'OpMemberDecorate {type_id} {index} ColMajor' in assembly
            shader_count += 1

        # Individual stage compilation cannot detect mismatched graphics interfaces.
        model_dir = shader_dir / 'model_instance'
        for stages in [('model.vert', 'model.frag'),
                       ('mesh_instance.task', 'mesh_instance.mesh', 'model.frag')]:
            subprocess.run([args.glslang, '-V', '--target-env', 'vulkan1.2', '-l',
                            *[str(model_dir / stage) for stage in stages],
                            '-o', str(temp / 'model_linked.spv')], check=True)

        assert shared_types == {name for name, _ in offsets}, 'Missing shader layout coverage'
        assert shared_types == set(strides), 'Missing struct array strides'
        assert shared_types == set(alignments), 'Missing struct alignments'
        # Assertions are generated from SPIR-V, not a second handwritten layout.
        probe = ['#include "shaders/shader_shared.h"', '#include <type_traits>']
        for (type_name, member), offset in sorted(offsets.items()):
            probe.append(f'static_assert(offsetof(shader::{type_name}, {member}) == {offset});')
        for type_name, stride in sorted(strides.items()):
            probe.append(f'static_assert(sizeof(shader::{type_name}) == {stride});')
        for type_name, alignment in sorted(alignments.items()):
            probe.append(f'static_assert(alignof(shader::{type_name}) == {alignment});')
            probe.append(f'static_assert(std::is_standard_layout_v<shader::{type_name}>);')
            probe.append(f'static_assert(std::is_trivially_copyable_v<shader::{type_name}>);')
        for type_name in sorted(root_types):
            probe.append(f'static_assert(alignof(shader::{type_name}) == 16);')
            probe.append(f'static_assert(sizeof(shader::{type_name}) % 16 == 0);')
        for (type_name, member), stride in sorted(member_strides.items()):
            probe.append(f'static_assert(sizeof(shader::{type_name}::{member}[0]) == {stride});')
        for (type_name, member), stride in sorted(matrix_strides.items()):
            probe.append(f'static_assert(sizeof(decltype(shader::{type_name}::{member})::col_type) == {stride});')
        path = temp / 'layouts.cpp'
        path.write_text('\n'.join(probe) + '\n')
        command = [args.cxx, '-std=c++20', '-fsyntax-only', '-I' + str(root / 'src')]
        if args.glm_include:
            command += ['-I' + str(args.glm_include)]
        # Check standalone, application, and aligned configurations. XYZW_ONLY disables SIMD.
        for flags in ([], ['-DGLM_FORCE_INTRINSICS', '-DGLM_FORCE_EXPLICIT_CTOR', '-DGLM_FORCE_XYZW_ONLY'],
                      ['-DGLM_FORCE_INTRINSICS', '-DGLM_FORCE_DEFAULT_ALIGNED_GENTYPES',
                       '-DGLM_FORCE_EXPLICIT_CTOR']):
            subprocess.run(command + flags + [str(path)], check=True)
    print(f'PASS: {shader_count} shaders, {len(offsets)} member offsets, '
          f'{len(strides)} struct strides, {len(member_strides)} member array strides, '
          f'{len(matrix_strides)} matrix strides, {len(alignments)} alignments; '
          'default, application, and aligned GLM configurations')


if __name__ == '__main__':
    main()
