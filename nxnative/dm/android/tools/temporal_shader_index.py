"""Index temporal-pass evidence from the actual translated Vulkan modules.

Descriptors and constant offsets are evidence for an adapter. A shader
signature alone cannot establish which bound image is the scene or its history.
"""
from collections import defaultdict
import hashlib
import struct

import dmsv
import resolution_meta

NAME = 'temporal-shaders.json'
MAX_MODULE_BYTES = 2 << 20
MAX_SCAN_BYTES = 128 << 20
MAX_CANDIDATES = 1024
MAX_OFFSETS = 96
MAX_COORDINATE_STEPS = 16384
REFLECTION = struct.Struct('<BBBBHHhBBii')

# SPIR-V opcodes and decorations used by normal translated descriptor accesses.
DEC_ARRAY_STRIDE, DEC_BINDING, DEC_OFFSET = 6, 33, 35
OP_TYPE_INT, OP_TYPE_FLOAT, OP_TYPE_VECTOR = 21, 22, 23
OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY, OP_TYPE_STRUCT, OP_TYPE_POINTER = 28, 29, 30, 32
TYPE_OPERANDS = {
    OP_TYPE_INT: 3, OP_TYPE_FLOAT: 2, OP_TYPE_VECTOR: 3,
    OP_TYPE_ARRAY: 3, OP_TYPE_RUNTIME_ARRAY: 2, OP_TYPE_STRUCT: 1,
}
OP_CONSTANT, OP_VARIABLE, OP_LOAD = 43, 59, 61
OP_ACCESS_CHAIN, OP_INBOUNDS_ACCESS_CHAIN = 65, 66
OP_DECORATE, OP_MEMBER_DECORATE, OP_COPY_OBJECT = 71, 72, 83
OP_SAMPLED_IMAGE, OP_IMAGE_WRITE, OP_IMAGE = 86, 99, 100
IMAGE_READS = {
    87: 'sample', 88: 'sample-lod', 89: 'sample-compare', 90: 'sample-lod-compare',
    91: 'projected-sample', 92: 'projected-sample-lod',
    93: 'projected-sample-compare', 94: 'projected-sample-lod-compare',
    95: 'fetch', 96: 'gather', 97: 'gather-compare', 98: 'read',
}


def reflected_bindings(data):
    if len(data) < 20:
        return {}
    count, constants = struct.unpack_from('<II', data)
    end = 20 + REFLECTION.size * count
    trailer = end + constants
    if trailer > len(data):
        return {}
    if trailer < len(data):
        # RSCL follows the C1 bytes. Its scaling slots are not Vulkan
        # descriptor bindings and provide no temporal-role evidence.
        if len(data) - trailer < resolution_meta.HEADER.size:
            return {}
        magic, version, records = resolution_meta.HEADER.unpack_from(data, trailer)
        if magic != b'RSCL' or version != 1 or records > 96 or (
                len(data) - trailer != resolution_meta.HEADER.size + records * resolution_meta.RECORD.size):
            return {}
    bindings = {}
    for kind, desc, dim, flags, slot, count, reg, _, fmt, off, _ in REFLECTION.iter_unpack(
            memoryview(data)[20:end]):
        # Dim 1 is 2D. Descriptor arrays of 2D views are allowed; array
        # image types, multisampled views and translated 1D fetches are excluded.
        if dim == 1 and not flags & 11 and desc in (2, 3, 6):
            bindings[slot] = dict(binding=slot, type='storage' if desc == 3 else 'sampled',
                                  count=count, nvn_kind=kind, nvn_register=reg, handle_offset=off,
                                  image_format=fmt)
        elif desc == 0 and kind in (1, 3):
            bindings[slot] = dict(binding=slot, type='uniform', nvn_kind=kind, nvn_register=reg)
    return bindings


def coordinate_inputs(op, operands):
    # Keep only actual IDs. Shuffle/extract indices and Phi block labels
    # are intentionally excluded.
    if op in (77, 79, 82) or 128 <= op <= 148:  # vector/composite ops, binary arithmetic
        return operands[:2]
    if op in (80, 169):  # CompositeConstruct, Select
        return operands
    if op in (81, OP_COPY_OBJECT, 124, 126, 127) or 109 <= op <= 115:
        return operands[:1]
    if op == 245:  # Phi
        return operands[::2]
    return ()


def trace_coordinates(coordinates, dependencies, samples, uniforms):
    image_sources, uniform_sources = defaultdict(set), defaultdict(set)
    remaining = MAX_COORDINATE_STEPS
    for slot, coord in sorted(coordinates):
        todo, visited = [coord], set()
        while todo:
            value = todo.pop()
            if value in visited:
                continue
            visited.add(value)
            if not remaining:
                return image_sources, uniform_sources, True
            remaining -= 1
            if value in samples:
                image_sources[slot].add(samples[value])
            elif value in uniforms:
                uniform_sources[slot].add(uniforms[value])
            else:
                todo.extend(dependencies.get(value, ()))
    return image_sources, uniform_sources, False


def accesses(code, bindings):
    if len(code) < 20 or len(code) % 4:
        raise ValueError('truncated SPIR-V')
    words = struct.unpack(f'<{len(code) // 4}I', code)
    if words[0] != 0x07230203:
        raise ValueError('invalid SPIR-V')
    types, types_of_pointer, constants = {}, {}, {}
    decorations, struct_offsets = defaultdict(dict), {}
    # Track only alias operations with defined operands. Dynamic UBO accesses
    # remain explicitly unresolved, including matrix element layout.
    pointers, images = {}, {}
    sampled, written = defaultdict(set), set()
    dependencies, sample_values, uniform_values = {}, {}, {}
    coordinates = set()
    uniform_offsets, dynamic_uniforms = defaultdict(set), set()
    unknown_images = unknown_uniforms = False
    at = 5
    while at < len(words):
        length, op = words[at] >> 16, words[at] & 0xffff
        if not length or at + length > len(words):
            raise ValueError('invalid SPIR-V instruction')
        args = words[at + 1:at + length]
        at += length
        if len(args) >= 3 and (inputs := coordinate_inputs(op, args[2:])):
            dependencies[args[1]] = inputs
        if op == OP_DECORATE and len(args) == 3:
            target, decoration, value = args
            if decoration in (DEC_ARRAY_STRIDE, DEC_BINDING):
                decorations[target][decoration] = value
        elif op == OP_MEMBER_DECORATE and len(args) == 4 and args[2] == DEC_OFFSET:
            struct_offsets[args[:2]] = args[3]
        elif op in TYPE_OPERANDS:
            if len(args) < TYPE_OPERANDS[op]:
                raise ValueError('truncated SPIR-V type')
            types[args[0]] = (op, args[1:])
        elif op == OP_TYPE_POINTER and len(args) == 3:
            types_of_pointer[args[0]] = args[2]
        elif op == OP_CONSTANT and len(args) == 3 and types.get(args[0], (0, ()))[0] == OP_TYPE_INT:
            constants[args[1]] = args[2]
        elif op == OP_VARIABLE and len(args) >= 3:
            pointer_type, result = args[:2]
            d = decorations.get(result, {})
            # Replay rewrites all sets to the stage's own set, keeping bindings.
            slot = d.get(DEC_BINDING)
            if slot in bindings:
                pointers[result] = (slot, types_of_pointer.get(pointer_type), 0)
        elif op in (OP_ACCESS_CHAIN, OP_INBOUNDS_ACCESS_CHAIN) and len(args) >= 3:
            pointer_type, result, base = args[:3]
            if base not in pointers:
                continue
            slot, typ, offset = pointers[base]
            if bindings[slot]['type'] == 'uniform' and offset is not None:
                for index in args[3:]:
                    index = constants.get(index)
                    if index is None:
                        offset = None
                        break
                    op_type, members = types.get(typ, (0, ()))
                    if op_type == OP_TYPE_STRUCT and index < len(members) and (typ, index) in struct_offsets:
                        offset += struct_offsets[typ, index]
                        typ = members[index]
                    elif op_type in (OP_TYPE_ARRAY, OP_TYPE_RUNTIME_ARRAY) and DEC_ARRAY_STRIDE in decorations.get(typ, {}):
                        offset += index * decorations[typ][DEC_ARRAY_STRIDE]
                        typ = members[0]
                    elif op_type == OP_TYPE_VECTOR and index < members[1]:
                        typ = members[0]
                        if typ not in types or types[typ][1][0] % 8:
                            offset = None
                            break
                        offset += index * (types[typ][1][0] // 8)
                    else:
                        offset = None
                        break
            pointers[result] = (slot, types_of_pointer.get(pointer_type), offset)
        elif op == OP_LOAD and len(args) >= 3 and args[2] in pointers:
            slot, _, offset = pointers[args[2]]
            if bindings[slot]['type'] != 'uniform':
                images[args[1]] = slot
            elif offset is None or offset >= 65536:  # replay caps descriptor ranges at 64 KiB
                dynamic_uniforms.add(slot)
            elif len(uniform_offsets[slot]) < MAX_OFFSETS or offset in uniform_offsets[slot]:
                uniform_offsets[slot].add(offset)
                uniform_values[args[1]] = (slot, offset)
            else:
                unknown_uniforms = True
        elif op in (OP_COPY_OBJECT, OP_SAMPLED_IMAGE, OP_IMAGE) and len(args) >= 3:
            if args[2] in images:
                images[args[1]] = images[args[2]]
            if op == OP_COPY_OBJECT and args[2] in pointers:
                pointers[args[1]] = pointers[args[2]]
        elif op in IMAGE_READS and len(args) >= 4:
            if args[2] in images:
                slot = images[args[2]]
                sampled[slot].add(IMAGE_READS[op])
                sample_values[args[1]] = slot
                coordinates.add((slot, args[3]))
            else:
                unknown_images = True
        elif op == OP_IMAGE_WRITE and args:
            if args[0] in images:
                written.add(images[args[0]])
            else:
                unknown_images = True

    coord_images, coord_uniforms, truncated_coords = trace_coordinates(
        coordinates, dependencies, sample_values, uniform_values)
    rows = []
    for slot, binding in sorted(bindings.items()):
        item = dict(binding)
        if binding['type'] == 'uniform':
            item.update(byte_offsets=sorted(uniform_offsets[slot]), unresolved_access=slot in dynamic_uniforms)
        else:
            item.update(read_ops=sorted(sampled[slot]), writes=slot in written)
            if coord_images[slot]:
                item['coordinate_image_sources'] = sorted(coord_images[slot])
            if coord_uniforms[slot]:
                item['coordinate_uniform_sources'] = sorted(coord_uniforms[slot])
        rows.append(item)
    return dict(bindings=rows, unresolved_image_access=unknown_images,
                truncated_uniform_offsets=unknown_uniforms, truncated_coordinate_trace=truncated_coords)


def build_index(bundle_path, bundle_sha256):
    candidates = []
    remaining = MAX_SCAN_BYTES
    skipped = defaultdict(int)
    with dmsv.Bundle(bundle_path) as bundle:
        # Prioritize compute before fragment material permutations.
        for selected_stage in (5, 1):
            for entry in bundle.entries:
                key, stage, _, stored, _, _, raw = entry
                if stage != selected_stage:
                    continue
                reflection = bundle.refl(entry)
                bindings = reflected_bindings(reflection)
                kinds = [b['type'] for b in bindings.values()]
                images = [b for b in bindings.values() if b['type'] == 'sampled']
                if (not images or (len(images) == 1 and images[0]['count'] == 1)
                        or 'uniform' not in kinds or (stage == 5 and 'storage' not in kinds)):
                    continue
                size = raw or stored
                if size > MAX_MODULE_BYTES:
                    skipped['module-size'] += 1
                    continue
                if size > remaining or len(candidates) >= MAX_CANDIDATES:
                    skipped['scan-budget'] += 1
                    continue
                remaining -= size
                try:
                    code = bundle.spv(entry)
                    evidence = accesses(code, bindings)
                except ValueError:
                    skipped['invalid-module'] += 1
                    continue
                stage_info = struct.unpack_from('<III', reflection, 8)
                if stage == 5:
                    evidence['local_size'] = stage_info
                else:
                    evidence['fragment_output_mask'] = stage_info[0]
                candidates.append(dict(key=f'{key:016x}', stage=stage, spv_sha256=hashlib.sha256(code).hexdigest(),
                                       **evidence))
    return dict(schema=1, bundle_sha256=bundle_sha256, candidates=candidates, skipped=dict(skipped))
