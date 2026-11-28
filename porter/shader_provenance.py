"""Export validated shader-product identity without rereading gigabytes of SPIR-V.

The receipt key identifies translation inputs, not a runtime ABI version.
A different key does not imply incompatibility. Legacy packs have no receipt.
"""
import re
import struct
from pathlib import Path
from porter import product_cache

def describe(path, receipt):
    path = Path(path)
    state = product_cache.file_state(path)
    files = receipt['files']
    if state is None or receipt.get('schema') != 2 or receipt['stat'] != state:
        raise ValueError('shader product receipt does not match current file')
    if len(files) != 1 or files[0][:2] != [path.name, state[0]]:
        raise ValueError('shader product receipt has invalid inventory')
    # The receipt was validated by product_cache.reused/stamp, including
    # content verification when stat identity changed. Keep the header in
    # that same receipt so unchanged products need no file reads.
    cached = receipt.get('dmsv_header')
    if cached is None:
        with path.open('rb') as stream:
            header = stream.read(12)
    else:
        if not isinstance(cached, str) or not re.fullmatch(r'[0-9a-f]{24}', cached):
            raise ValueError('invalid cached shader product header')
        header = bytes.fromhex(cached)
    record = dict(schema=1, input_key=receipt['key'], sha256=files[0][2],
                  bytes=state[0], header=header.hex(), complete=receipt['complete'])
    validate(record, header, state[0])
    if product_cache.file_state(path) != state:
        raise ValueError('shader product changed while recording provenance')
    return record

def validate(record, header, size):
    if not isinstance(record, dict) or type(record.get('schema')) is not int or record['schema'] != 1:
        raise ValueError('unsupported shader product provenance schema')
    if not isinstance(record.get('input_key'), str) or not 0 < len(record['input_key']) <= 256:
        raise ValueError('invalid shader product input key')
    if not isinstance(record.get('sha256'), str) or not re.fullmatch(r'[0-9a-f]{64}', record['sha256']):
        raise ValueError('invalid shader product digest')
    if type(record.get('complete')) is not bool or type(record.get('bytes')) is not int:
        raise ValueError('invalid shader product receipt fields')
    if len(header) != 12:
        raise ValueError('invalid shader product header')
    magic, version, count = struct.unpack('<4sII', header)
    if magic != b'DMSV' or version not in (1, 2) or 12 + 32 * count > size:
        raise ValueError('incompatible shader product header')
    if record.get('header') != header.hex() or record['bytes'] != size:
        raise ValueError('shader product provenance does not match bundle header or size')
    # This is structural compatibility, not a fresh byte-for-byte hash check.
    return record
