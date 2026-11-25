"""Production texture transfers must retain logical format semantics."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def between(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]


class TextureTransfers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = os.environ.get('CC') or shutil.which('cc') or shutil.which('clang')
        if not cls.cc:
            raise unittest.SkipTest('host C compiler required')
        headers = ROOT / 'nxnative/dm/third_party/vulkan-headers/include'
        exe = (ROOT / 'nxnative/dm/android/gfx_vk_exec.c').read_text(encoding='utf-8')
        header = (ROOT / 'nxnative/dm/android/gfx_vk.h').read_text(encoding='utf-8')
        harness = (ROOT / 'tests/texture_transfers_harness.c').read_text(encoding='utf-8')
        types = between(header, 'typedef struct { VkFormat vk;', 'const vkfmt_t *vk_fmt')
        types += between(header, 'typedef struct vkimg vkimg_t;', '// Replay thread, outside any render pass.')
        tex = (ROOT / 'nxnative/dm/android/gfx_vk_tex.c').read_text(encoding='utf-8')
        functions = between(tex, 'static uint64_t g_wseq;', 'static void alias_link(')
        functions += between(exe, 'static VkImageSubresourceLayers sub_of(', 'static void copy_b2t(')
        functions += between(exe, 'enum { CF_MIRROR_X', '// ---------------- the record walker')
        cls.tmp = tempfile.TemporaryDirectory(prefix='nx2a-transfers-')
        cls.addClassCleanup(cls.tmp.cleanup)
        c = Path(cls.tmp.name) / 'test.c'
        c.write_text(harness.replace('/* TYPES */', types).replace('/* FUNCTIONS */', functions), encoding='utf-8')
        cls.program = c.with_suffix('.exe' if os.name == 'nt' else '')
        subprocess.run([cls.cc, '-std=c11', '-I'+str(ROOT), '-I'+str(ROOT/'nxnative/dm/gfx/nvn'), '-I'+str(ROOT/'nxnative/dm/gfx/nvn/include'), '-I'+str(ROOT/'nxnative/dm/gfx/tex'), '-idirafter', str(headers), str(c), '-lm', '-o', str(cls.program)], check=True, text=True)

    def check_case(self, name):
        p = subprocess.run([str(self.program), name], capture_output=True, text=True, timeout=15)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)

    def test_raw_copy_between_srgb_backings(self):
        self.check_case('copy')

    def test_d24s8_fallback_copies_both_aspects(self):
        self.check_case('depth_stencil_copy')

    def test_resolve_uses_equal_image_formats(self):
        self.check_case('resolve')

    def test_unsupported_resolve_formats_are_rejected(self):
        self.check_case('resolve_invalid')

    def test_clear_view_overrides_texture_encoding(self):
        self.check_case('clear_view')

    def test_clear_encodes_rgb_once_and_preserves_alpha(self):
        self.check_case('clear')

    def test_scaled_mirrored_and_converting_blits(self):
        self.check_case('blit')

    def test_temporaries_use_pooled_and_dedicated_retirement(self):
        self.check_case('memory')

    def test_temporary_allocation_failures_skip_transfers(self):
        self.check_case('memory_failure')

    def test_direct_physical_display_transfer(self):
        self.check_case('physical_display')
