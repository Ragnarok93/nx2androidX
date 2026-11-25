"""Optional DMSV trailer: preserve Ryujinx descriptor order and usage flags."""
import struct

HEADER = struct.Struct('<4sHH')
RECORD = struct.Struct('<HBB')
UNKNOWN, UNSUPPORTED, NEEDS_SCALE, WRITES = 255, 1, 2, 4


def encode_resolution_meta(stage, binds, res):
    if stage not in ('vs', 'ps', 'cs') or not all(isinstance(res.get(k), list) for k in ('textures', 'images')):
        return b''
    textures, images = res['textures'], res['images']
    # Ryujinx emits non-array descriptors before arrays. Image helper indices
    # start after the non-array texture count, not the SPIR-V binding number.
    tex_count = sum(t.get('array') == 1 for t in textures)
    records = {}
    for resources, offset, is_image in ((textures, 0, False), (images, tex_count, True)):
        for i, t in enumerate(resources):
            binding, flags, count = t.get('binding'), t.get('flags'), t.get('array')
            if not isinstance(binding, int) or not 0 <= binding <= 65535 or not isinstance(flags, int) or binding in records:
                return b''
            slot = 1 + offset + i
            if count != 1 or not 1 <= slot <= 72 or flags & ~15 or is_image:
                flags |= UNSUPPORTED
                slot = UNKNOWN
            records[binding] = (slot, flags & 15)
    out = []
    for b in binds:
        if b['type'] not in ('sampled', 'storage', 'image', 'tbuf', 'storage_tbuf'):
            continue
        binding = b['binding']
        slot, flags = records.get(binding, (UNKNOWN, UNSUPPORTED))
        if b.get('count') != 1 or b.get('dim') != 1 or b.get('arrayed') or b.get('ms') or b['type'] not in ('sampled', 'image'):
            slot, flags = UNKNOWN, flags | UNSUPPORTED
        out.append(RECORD.pack(binding, slot, flags))
    return HEADER.pack(b'RSCL', 1, len(out)) + b''.join(out)
