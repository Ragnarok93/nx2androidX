"""Reconstruction target and physical presentation remain independent."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class OutputTarget(unittest.TestCase):
    def test_resolution_and_settings(self):
        source=r'''
#include <assert.h>
#include <limits.h>
#include "gfx_vk_output_target.h"
int main(void) {
    uint32_t w,h;
    assert(dm_recon_target_extent(1920,1080,1920,1080,2160,&w,&h));
    assert(w==3840&&h==2160); // 4K intermediate on a 1080p surface
    assert(dm_recon_target_extent(1280,720,1920,1080,1440,&w,&h));
    assert(w==2560&&h==1440);
    assert(dm_recon_target_extent(1920,1080,1440,810,0,&w,&h)&&w==1440&&h==810);
    assert(dm_recon_target_extent(1080,1920,1080,1920,1440,&w,&h)&&w==1440&&h==2560);
    assert(dm_recon_target_extent(1920,1080,1920,1080,2880,&w,&h)&&w==5120&&h==2880);
    assert(dm_recon_target_extent(1920,1080,1920,1080,4320,&w,&h)&&w==7680&&h==4320);
    assert(dm_recon_target_extent(1920,1080,1920,1080,720,&w,&h)&&w==1280&&h==720);
    assert(dm_recon_target_extent(1325,720,3840,2086,2160,&w,&h)&&w==3975&&h==2160);
    assert(!dm_recon_target_extent(1920,1080,1920,1080,INT_MAX,&w,&h)&&!w&&!h);
    assert(!dm_recon_target_extent(0,1080,1920,1080,2160,&w,&h));
    assert(!dm_recon_target_extent(1920,1080,0,1080,2160,&w,&h));
    assert(dm_recon_output_step(0,1)==720&&dm_recon_output_step(720,-1)==0);
    assert(dm_recon_output_step(4320,1)==0&&dm_recon_output_step(0,-1)==4320);
    assert(dm_recon_output_step(1500,1)==1800&&dm_recon_output_step(1500,-1)==1440);
    assert(dm_recon_custom_step(0,1)==1080&&dm_recon_custom_step(1440,1)==1560);
    assert(dm_recon_custom_step(120,-1)==0&&dm_recon_custom_step(INT_MAX,1)==INT_MAX);
    assert(dm_recon_parse_height("0")==0&&dm_recon_parse_height("2160\n")==2160);
    assert(dm_recon_parse_height("-1")==-1&&dm_recon_parse_height("99999999999999999999")==-1);
    assert(dm_recon_parse_height("1440junk")==-1&&dm_recon_parse_height("")==-1);
    assert(dm_recon_parse_height("119")==-1&&dm_recon_parse_height("9000")==9000);
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'nxnative/dm/android'),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)
if __name__=='__main__':unittest.main()
