"""Verify ELF note parsing and identity of the actual loaded runtime module."""
import ctypes
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

@unittest.skipUnless(shutil.which('cc') and shutil.which('readelf'), 'C compiler and readelf required')
class RuntimeIdentity(unittest.TestCase):
    def test_loaded_library_and_malformed_notes(self):
        source = r'''
#include <assert.h>
#include "gfx_vk_runtime_identity.h"
void runtime_id(char *out) { dm_runtime_build_id(out); }
int verify_notes(void) {
    char out[DM_RUNTIME_ID_CAP] = "unavailable";
    struct { ElfW(Nhdr) h; char name[4]; unsigned char desc[4]; } n = {
        {4, 4, NT_GNU_BUILD_ID}, {'G','N','U',0}, {0,1,0xab,0xff}};
    assert(dm_runtime_note_id(&n, sizeof n, out) && !strcmp(out, "0001abff"));
    strcpy(out,"unavailable");
    assert(!dm_runtime_note_id(&n, sizeof n-1, out) && !strcmp(out,"unavailable"));
    n.h.n_namesz=UINT32_MAX;
    assert(!dm_runtime_note_id(&n, sizeof n, out));
    n.h.n_namesz=4; n.h.n_descsz=UINT32_MAX;
    assert(!dm_runtime_note_id(&n, sizeof n, out));
    n.h.n_descsz=4; n.h.n_type=0;
    assert(!dm_runtime_note_id(&n, sizeof n, out));
    return 1;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp); (p/'id.c').write_text(source)
            inc=Path(__file__).resolve().parents[1]/'nxnative/dm/android'
            so=p/'libidentity.so'
            subprocess.run(['cc','-std=c11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-fPIC','-shared','-Wl,--build-id=sha1','-I',str(inc),str(p/'id.c'),'-ldl','-o',str(so)],check=True)
            expected=re.search(r'Build ID: ([0-9a-f]+)',subprocess.check_output(['readelf','-n',str(so)],text=True)).group(1)
            lib=ctypes.CDLL(str(so)); lib.runtime_id.argtypes=[ctypes.c_char_p]; lib.runtime_id.restype=None
            out=ctypes.create_string_buffer(129);lib.runtime_id(out)
            self.assertEqual(out.value.decode(),expected)
            self.assertEqual(lib.verify_notes(),1)

    def test_android_guest_elf_types_do_not_collide(self):
        # Exercise the Android declaration/implementation split with a host ELF
        # compiler, including the guest typedefs that conflict with system ELF.
        source = r'''
#include <stdint.h>
typedef struct { int64_t d_tag; union { uint64_t d_val, d_ptr; } d_un; } Elf64_Dyn;
typedef struct { uint64_t r_offset, r_info; int64_t r_addend; } Elf64_Rela;
typedef struct { uint32_t st_name; uint8_t st_info, st_other; uint16_t st_shndx; uint64_t st_value, st_size; } Elf64_Sym;
#include "gfx_vk_runtime_identity.h"
void runtime_id(char *out) { dm_runtime_build_id(out); }
'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p/'caller.c').write_text(source)
            inc = Path(__file__).resolve().parents[1]/'nxnative/dm/android'
            so = p/'libidentity.so'
            subprocess.run(['cc','-std=c11','-D_GNU_SOURCE','-D__ANDROID__','-Wall','-Wextra','-Werror',
                            '-fPIC','-shared','-Wl,--build-id=sha1','-I',str(inc),str(p/'caller.c'),
                            str(inc/'gfx_vk_runtime_identity.c'),'-ldl','-o',str(so)],check=True)
            expected = re.search(r'Build ID: ([0-9a-f]+)',subprocess.check_output(['readelf','-n',str(so)],text=True)).group(1)
            lib = ctypes.CDLL(str(so))
            lib.runtime_id.argtypes = [ctypes.c_char_p]
            lib.runtime_id.restype = None
            out = ctypes.create_string_buffer(129)
            lib.runtime_id(out)
            self.assertEqual(out.value.decode(),expected)

if __name__=='__main__':unittest.main()
