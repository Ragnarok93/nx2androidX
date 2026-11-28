"""Validate the exact Graphics order, one output entry and picker routes."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class OutputMenu(unittest.TestCase):
    def test_graphics_only_new_controls_and_selection_bits(self):
        code=(ROOT/'nxnative/dm/android/overlay_layout.h').read_text()
        start=code.index('enum { OVM_OVERLAY,');end=code.index('#define OV_MENU_ROW',start)
        source=r'''
#include <assert.h>
#include <string.h>
#define __ANDROID__ 1
'''+code[start:end]+r'''
int dm_menu_audio_available(void){return 1;}
int dm_menu_rs_live(void){return 1;}
int dm_menu_fg_flow(void){return 0;}
int main(void){
    assert(OVM_ROWS<=15);
    assert(!strcmp(OVM_TAB_NAMES[2],"Graphics"));
    const int expected[]={OVM_VIVID,OVM_FSR,OVM_FSR_SHARP,OVM_OUTPUT_HEIGHT,OVM_DRS,OVM_DRS_MIN,OVM_RS_MAX};
    for(int i=0;i<7;i++)assert(ov_menu_item(2,i+1)==expected[i]);
    for(int i=8;i<OVM_ROWS-1;i++)assert(ov_menu_item(2,i)==-1);
    unsigned target=0,custom=0,driver=0;
    for(int tab=0;tab<OVM_PAGES;tab++){
        assert(ov_menu_item(tab,0)==OVM_TAB);
        assert(ov_menu_item(tab,OVM_ROWS-1)==OVM_RESUME);
        for(int row=1;row<OVM_ROWS-1;row++){
            int item=ov_menu_item(tab,row);
            if(item==OVM_OUTPUT_HEIGHT){assert(tab==2);target++;}
            if(item==OVM_OUTPUT_CUSTOM){assert(tab==OVM_OUTPUT_PAGE);custom++;}
            if(item==OVM_DRIVER){assert(tab==1);driver++;}
            assert(item!=OVM_DRS_HOOK);
            if(tab!=0)assert(item!=OVM_OVERLAY);
        }
    }
    assert(target==1&&custom==1&&driver==1);
    const int heights[]={0,720,1080,1440,1800,2160,2880,4320};
    for(int i=0;i<8;i++)assert(ov_output_preset(ov_menu_item(OVM_OUTPUT_PAGE,i+1))==heights[i]);
    assert(ov_menu_item(OVM_OUTPUT_PAGE,9)==OVM_OUTPUT_CUSTOM);
    assert(ov_menu_item(OVM_OUTPUT_PAGE,10)==OVM_OUTPUT_BACK);
    assert(ov_output_preset(OVM_OUTPUT_CUSTOM)==-1);
    assert(ov_output_preset(OVM_OUTPUT_HEIGHT)==-1);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'case.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)
    def test_actual_picker_selection_branch(self):
        base=ROOT/'nxnative/dm/android'
        layout=(base/'overlay_layout.h').read_text()
        table=layout[layout.index('enum { OVM_OVERLAY,'):layout.index('#define OV_MENU_ROW')]
        front=(base/'front_android.c').read_text()
        a=front.index('    else if (item == OVM_OUTPUT_HEIGHT) {')
        b=front.index('    else if (item == OVM_VIVID)',a)
        actions=front[a:b]
        source=r''' 
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include "gfx_vk_output_target.h"
#define __ANDROID__ 1
'''+table+r'''
int dm_menu_audio_available(void){return 1;}
int dm_menu_rs_live(void){return 1;}
int dm_menu_fg_flow(void){return 0;}
static _Atomic int g_diag_flags_page,g_menu_hi,g_set_output_height,g_menu_cfg_ver;
static int saves;
static void settings_save(void){saves++;}
static void menu_output_step(int direction,int custom){assert(custom);g_set_output_height=dm_recon_custom_step(g_set_output_height,direction);saves++;}
static void select_item(int item){if(0){}
'''+actions+r'''
}
int main(void){
    assert(!strcmp(OVM_TAB_NAMES[2],"Graphics"));
    select_item(OVM_OUTPUT_HEIGHT);
    assert(g_diag_flags_page==3&&g_menu_hi==-1&&saves==0);
    select_item(OVM_OUTPUT_2160);
    assert(g_diag_flags_page==0&&g_menu_hi==4&&g_set_output_height==2160&&saves==1&&g_menu_cfg_ver==1);
    select_item(OVM_OUTPUT_HEIGHT);select_item(OVM_OUTPUT_CUSTOM);
    assert(g_diag_flags_page==3&&g_set_output_height==2280&&saves==2);
    select_item(OVM_OUTPUT_BACK);
    assert(g_diag_flags_page==0&&g_menu_hi==4&&g_set_output_height==2280&&saves==2);
    select_item(OVM_OUTPUT_HEIGHT);select_item(OVM_OUTPUT_AUTO);
    assert(g_set_output_height==0&&g_diag_flags_page==0&&saves==3);
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'picker.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(base),str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)
if __name__=='__main__':unittest.main()
