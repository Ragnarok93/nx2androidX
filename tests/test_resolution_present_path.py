"""Verify identity presentation copies, WSI lock ordering and worker failure handling."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class PresentPath(unittest.TestCase):
    def test_exact_copy_and_worker_start(self):
        src=(ROOT/'nxnative/dm/android/gfx_vk_present.c').read_text()
        def function(signature):
            i=src.index(signature)
            return src[i:src.index('\n}',i)+2]
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <pthread.h>
typedef uintptr_t VkImage;
typedef struct {VkImage img;int fmt;} vkimg_t;
static int g_sfmt=37;
static int fail,created,detached,logs;
static int g_present_thread_started;
static void *present_thread(void *arg){return arg;}
static int create(pthread_t *t,const pthread_attr_t *a,void *(*fn)(void *),void *arg){
    (void)t;(void)a;(void)fn;(void)arg;++created;return fail?11:0;
}
static int detach(pthread_t t){(void)t;++detached;return 0;}
#define pthread_create create
#define pthread_detach detach
#define VKLOG(...) (++logs)
''' + function('static int present_exact_copy(') + '\n' + function('static void present_thread_start(') + r'''
int main(void){
    vkimg_t im={1,37};
    assert(present_exact_copy(&im,1,1920,1080,1920,1080,0));
    assert(!present_exact_copy(&im,2,1920,1080,1920,1080,0));
    assert(!present_exact_copy(&im,1,1920,1080,1920,1080,1));
    assert(!present_exact_copy(&im,1,1280,720,1920,1080,0));
    im.fmt=43;assert(!present_exact_copy(&im,1,1920,1080,1920,1080,0));
    assert(!present_exact_copy(NULL,1,1920,1080,1920,1080,0));
    fail=1;present_thread_start();assert(created==1&&!detached&&!g_present_thread_started&&logs==1);
    fail=0;present_thread_start();assert(created==2&&detached==1&&g_present_thread_started);
    return 0;
}
'''
        # The mocked successful pthread_create needs a defined thread token.
        code=code.replace('(void)t;(void)a;', '*t=(pthread_t)0;(void)a;')
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'case.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    def test_poll_is_outside_submit_mutex(self):
        src=(ROOT/'nxnative/dm/android/gfx_vk_present.c').read_text()
        start=src.index('static int present_one(')
        body=src[start:src.index('\n}',start)+2]
        self.assertLess(body.index('pace_poll();'),body.index('pthread_mutex_lock(&g_vk_queue_mu)'))
        self.assertLess(body.index('vkQueueSubmit('),body.index('pace_desired(period)'))
        self.assertLess(body.index('vkQueuePresentKHR('),body.index('pthread_mutex_unlock(&g_vk_queue_mu)',body.index('vkQueuePresentKHR(')))
        self.assertIn('if (vkBeginCommandBuffer(cb,&bi) != VK_SUCCESS)',body)
        self.assertIn('if (vkEndCommandBuffer(cb) != VK_SUCCESS)',body)
        self.assertIn('if (vkResetFences(V.dev,1,&g_pfence[k]) != VK_SUCCESS)',body)
