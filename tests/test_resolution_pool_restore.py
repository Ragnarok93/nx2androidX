"""Exercise the production guest pool remap against real Linux mappings."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PoolRestore(unittest.TestCase):
    def test_restore_preserves_bytes_and_does_not_discard_on_allocation_failure(self):
        source = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include "gfx_vk_pool_restore.h"
static int fail_alloc;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n) {
    return fail_alloc ? NULL : __real_malloc(n);
}
void *__real_mmap(void *, size_t, int, int, int, off_t);
void *__wrap_mmap(void *a, size_t n, int p, int f, int fd, off_t off) {
    if (fail_alloc && !(f & MAP_FIXED) && (f & MAP_ANONYMOUS)) {
        errno = ENOMEM; return MAP_FAILED;
    }
    return __real_mmap(a,n,p,f,fd,off);
}
int main(void) {
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const size_t n = (16u << 20) + page;
    int fd = memfd_create("pool", 0);
    assert(fd >= 0 && ftruncate(fd, (off_t)n) == 0);
    unsigned char *src = mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    unsigned char *alias = mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    assert(src != MAP_FAILED && alias != MAP_FAILED);
    for(size_t i=0;i<n;i++) src[i] = (unsigned char)(i%251+1);
    // Simulate inability to allocate staging; the source mapping must survive.
    fail_alloc = 1;
    int rc = vk_pool_restore_anon(src,n);
    fail_alloc = 0;
    for(size_t i=0;i<n;i++) assert(src[i] == (unsigned char)(i%251+1));
    assert(rc != 0);
    assert(vk_pool_restore_anon(src,n) == 0);
    memset(alias,0,n);
    for(size_t i=0;i<n;i++) assert(src[i] == (unsigned char)(i%251+1));
    src[0] = 7; assert(alias[0] == 0);
    munmap(src,n); munmap(alias,n); close(fd);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'case.c'
            p.write_text(source)
            exe = p.with_suffix('')
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'nxnative/dm/android'), str(p), '-o', str(exe),
                '-Wl,--wrap=malloc', '-Wl,--wrap=mmap'], check=True)
            subprocess.run([str(exe)], check=True)
