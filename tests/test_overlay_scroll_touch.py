"""Compile actual Android menu_touch + menu_slider_set with fake event/input boundaries."""
import subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ScrollTouch(unittest.TestCase):
    def test_swipe_slider_and_resume(self):
        base=ROOT/'nxnative/dm/android'
        layout=(base/'overlay_layout.h').read_text(); a=layout.index('enum { OVM_OVERLAY,'); b=layout.index('// ---- the layout editor:',a)
        src=(base/'front_android.c').read_text(); a2=src.index('static void menu_slider_set('); b2=src.index('static void ed_key(',a2)
        touch=src[a2:b2]
        code=r'''
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#define __ANDROID__ 1
static float dm_overlay_scale(void){return 1;}
'''+layout[a:b]+r'''
int dm_menu_audio_available(void){return 0;}
int dm_menu_rs_live(void){return 1;}
enum{AMOTION_EVENT_ACTION_DOWN=0,AMOTION_EVENT_ACTION_UP=1,AMOTION_EVENT_ACTION_MOVE=2,AMOTION_EVENT_ACTION_CANCEL=3,AMOTION_EVENT_ACTION_POINTER_DOWN=5,AMOTION_EVENT_ACTION_POINTER_UP=6,AMOTION_EVENT_ACTION_MASK=255,AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT=8,OV_MENU=1};
typedef struct{int action,pointers;struct{int id;float x,y;}ptr[4];}dm_java_event;
static struct{int kind;}OV_CTLS[1];
static int ctl_at(float x,float y,float w,float h){(void)x;(void)y;(void)w;(void)h;return -1;}
static int g_menu_ptr=-1,g_menu_row,g_menu_down_scroll,g_menu_down_page,g_menu_gesture;
static float g_menu_down_x,g_menu_down_y,g_menu_down_w,g_menu_down_h;
static _Atomic int g_menu_hi;
static int scroll,page=2,actions,last=-1,saves,sharpness=50,slider_calls,closed;
static int dm_menu_scroll_units(void){return scroll;}
static void menu_scroll_set(int tab,int s){scroll=ov_settings_clamp(tab,s);}
static int menu_active_tab(void){return page;}
static void menu_tab(int p){page=p;scroll=0;}
static void dm_overlay_redraw(void){}
static void dm_haptics_android_tap(void){}
static void settings_save(void){saves++;}
static void menu_close(void){closed++;}
static void menu_do(int item){actions++;last=item;
    if(item==OVM_OUTPUT_HEIGHT){page=OVM_OUTPUT_PAGE;scroll=0;}
    else if(item==OVM_OUTPUT_BACK||ov_output_preset(item)>=0){page=2;scroll=0;}
}
static void menu_fsr_sharp(int x){sharpness=x;slider_calls++;}
static void menu_opacity(int x){(void)x;slider_calls++;}
static void menu_drs_min(int x){(void)x;slider_calls++;}
static void menu_rs_max(int x){(void)x;slider_calls++;}
'''
        if 'menu_output_step(' in touch:
            code+='static void menu_output_step(int d,int custom){(void)d;(void)custom;actions++;last=OVM_OUTPUT_CUSTOM;}\n'
        code+=touch+r'''
static void send(int a,float x,float y){dm_java_event e={.action=a,.pointers=1,.ptr={{.id=7,.x=x,.y=y}}};menu_touch(&e,1536,720);}
int main(void){
    assert(OVM_TAB_NAMES[2][0]=='G');
    float p[4],v[4],r[4];ov_menu_rect(1536,720,p);ov_settings_view(p,v);float rh=v[3]/OV_SETTINGS_VIEW_ROWS;
    ov_settings_row(p,2,3,0,r);float x=v[0]+v[2]*.6f,y=r[1]+r[3]/2;
    // Start on the slider track and swipe vertically. No setting changes.
    send(0,x,y);send(2,x,y-rh);send(1,x,y-rh);
    assert(scroll==OV_SETTINGS_UNIT&&sharpness==50&&slider_calls==0&&actions==0&&saves==0);
    // Drag back to the top. Clamp rather than overscroll or activate a row.
    send(0,x,v[1]+rh);send(2,x,v[1]+rh*5);send(1,x,v[1]+rh*5);
    assert(scroll==0&&actions==0&&slider_calls==0);
    // Horizontal slider gesture locks to its track and saves on release.
    send(0,v[0]+v[2]*.45f,y);send(2,v[0]+v[2]*.84f,y);send(1,v[0]+v[2]*.84f,y);
    assert(sharpness==100&&slider_calls>0&&saves==1&&scroll==0&&actions==0);
    // A single tap works too; it is not applied on down.
    int before=slider_calls;send(0,v[0]+v[2]*.40f,y);assert(slider_calls==before);send(1,v[0]+v[2]*.40f,y);assert(sharpness==0&&saves==2);
    // Last setting is reachable; a swipe ending on a row cannot trigger it.
    send(0,x,v[1]+rh*5);send(2,x,v[1]-rh*6);send(1,x,v[1]-rh*6);
    assert(scroll==ov_settings_max(2)&&actions==0&&closed==0);
    ov_settings_row(p,2,7,scroll,r);send(0,x,r[1]+rh/2);send(1,x,r[1]+rh/2);
    assert(actions==0&&saves==3); // last row is a slider, not a button
    ov_settings_row(p,2,4,scroll,r);send(0,x,r[1]+rh/2);send(1,x,r[1]+rh/2);
    assert(actions==1&&last==OVM_OUTPUT_HEIGHT&&page==OVM_OUTPUT_PAGE&&scroll==0);
    send(0,x,v[1]+rh*5);send(2,x,v[1]-rh*6);send(1,x,v[1]-rh*6);
    assert(scroll==ov_settings_max(OVM_OUTPUT_PAGE)&&actions==1);
    ov_settings_row(p,OVM_OUTPUT_PAGE,9,scroll,r);send(0,x,r[1]+rh/2);send(1,x,r[1]+rh/2);
    assert(actions==2&&last==OVM_OUTPUT_CUSTOM);
    ov_settings_row(p,OVM_OUTPUT_PAGE,10,scroll,r);send(0,x,r[1]+rh/2);send(1,x,r[1]+rh/2);
    assert(actions==3&&last==OVM_OUTPUT_BACK&&page==2&&scroll==0);
    ov_settings_row(p,2,OVM_ROWS-1,scroll,r);send(0,x,r[1]+rh/2);send(1,x,r[1]+rh/2);
    assert(actions==4&&last==OVM_RESUME);
    // A cancelled gesture never activates anything.
    send(0,x,v[1]+rh);send(3,x,v[1]+rh);assert(g_menu_ptr==-1&&actions==4);
    // Switch to a short tab; no scroll or old selection survives.
    ov_settings_row(p,2,0,scroll,r);send(0,v[0]+v[2]*.1f,r[1]+rh/2);send(1,v[0]+v[2]*.1f,r[1]+rh/2);
    assert(page==0&&scroll==0&&actions==4);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'touch.c';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(base),str(p),'-lm','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)
if __name__=='__main__':unittest.main()
