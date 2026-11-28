"""New export provenance and bounded legacy upgrade checks."""
import hashlib
import json
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

from porter import product_cache, shader_provenance, upgrade_apk
from porter.upstream import BuildError

class ShaderProvenance(unittest.TestCase):
    def test_receipt_identity_and_changed_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'shaders.dmsv'
            header=struct.pack('<4sII',b'DMSV',2,0)
            path.write_bytes(header)
            receipt=Path(tmp)/'receipt.json'
            product_cache.stamp(receipt,'fixture-input-key',path,complete=False)
            record=shader_provenance.describe(path,json.loads(receipt.read_text()))
            self.assertEqual(record['sha256'],hashlib.sha256(header).hexdigest())
            self.assertFalse(record['complete'])
            self.assertEqual(shader_provenance.validate(record,header,12),record)
            with self.assertRaises(ValueError):
                shader_provenance.validate(record,header,13)
            path.write_bytes(header+b'changed')
            with self.assertRaises(ValueError):
                shader_provenance.describe(path,json.loads(receipt.read_text()))

    def test_upgrade_allows_legacy_and_checks_new_provenance(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            shader=root/'shaders.dmsv'
            header=struct.pack('<4sII',b'DMSV',2,0)
            shader.write_bytes(header)
            receipt=root/'receipt.json'
            product_cache.stamp(receipt,'fixture-key',shader)
            product=shader_provenance.describe(shader,json.loads(receipt.read_text()))
            import io
            def make(manifest, data):
                inner=io.BytesIO()
                with zipfile.ZipFile(inner,'w') as pack:
                    pack.writestr('nx2a-data.json',json.dumps(manifest))
                    pack.writestr('mods/build_src.sha256','fixture')
                    if data is not None:
                        pack.writestr('shaders.dmsv',data)
                apk=root/'game.apk'
                with zipfile.ZipFile(apk,'w') as archive:
                    archive.writestr('lib/arm64-v8a/libdmhost.so',b'\x7fELF\x02\x01\x01'+b'\0'*9+struct.pack('<HH',3,183))
                    archive.writestr('assets/nx2a-data.zip',inner.getvalue())
                return apk
            self.assertEqual(upgrade_apk.metadata(make({},header),None)[0],{})
            man={'shader_product':product}
            self.assertEqual(upgrade_apk.metadata(make(man,header),None)[0],man)
            for data in (header+b'x',None):
                with self.assertRaises(BuildError):
                    upgrade_apk.metadata(make(man,data),None)
            for bad in ({**product,'schema':2},{**product,'sha256':'bad'}):
                with self.assertRaises(BuildError):
                    upgrade_apk.metadata(make({'shader_product':bad},header),None)
