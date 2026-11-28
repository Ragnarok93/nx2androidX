"""Production pause-menu geometry, clipping/hit-testing and swipe arbitration."""
from pathlib import Path
import subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class SettingsScroll(unittest.TestCase):
    def test_geometry_and_navigation(self):
        base=ROOT/'nxnative/dm/android'
        code=(base/'overlay_layout.h').read_text()
        start=code.index('enum { OVM_OVERLAY,');end=code.index('// ---- the layout editor:',start)
        source=r'''
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#define __ANDROID__ 1
static float dm_overlay_scale(void){return 1;}
'''+code[start:end]+r'''
static int live=1;
int dm_menu_audio_available(void){return 0;}
int dm_menu_rs_live(void){return live;}
int main(void){
     assert(OVM_TAB_NAMES[2][0]=='G'); float p[4],r[4]; ov_menu_rect(1536,720,p);
#ifdef OV_SETTINGS_VIEW_ROWS
    assert(OV_SETTINGS_VIEW_ROWS==6);
    assert(ov_settings_count(2)==7 && ov_settings_count(0)==5);
    assert(ov_menu_item(OVM_DIAG_TAB,1)==OVM_DIAG_SNAPSHOT);
    assert(ov_menu_item(OVM_DIAG_TAB,2)==OVM_DIAG_DELAY);
    assert(ov_menu_item(OVM_DIAG_TAB,3)==OVM_DIAG_CAPTURE);
    assert(ov_menu_item(OVM_DIAG_TAB,5)==OVM_DIAG_STOP);
    assert(ov_menu_item(OVM_DIAG_TAB,6)==OVM_DIAG_EXPORT);
    assert(ov_settings_max(0)==0 && ov_settings_max(2)==OV_SETTINGS_UNIT);
    const float screens[][2]={{1536,720},{720,1536},{640,360},{360,640},{2560,1440}};
    for(unsigned k=0;k<sizeof screens/sizeof screens[0];k++){
        float W=screens[k][0],H=screens[k][1],a[4],b[4],v[4];
        ov_menu_rect(W,H,p); ov_settings_view(p,v);
        ov_settings_row(p,2,0,0,a); ov_settings_row(p,2,OVM_ROWS-1,0,b);
        float track[3];ov_settings_track(a,track);
        assert(track[0]>=v[0]+v[2]*0.48f);
        assert(track[0]+track[2]<=v[0]+v[2]*0.78f);
        assert(fabsf(ov_settings_fraction(track[0],p))<0.001f);
        assert(fabsf(ov_settings_fraction(track[0]+track[2]*0.5f,p)-0.5f)<0.001f);
        assert(fabsf(ov_settings_fraction(track[0]+track[2],p)-1)<0.001f);
        assert(a[1]+a[3]<=v[1]+0.01f && v[1]+v[3]<=b[1]+0.01f);
        assert(p[0]>=0&&p[1]>=0&&p[0]+p[2]<=W+0.01f&&p[1]+p[3]<=H+0.01f);
        int max=ov_settings_max(2);
        for(int offset=0;offset<=max;offset+=OV_SETTINGS_UNIT/2){
            float tab[4],resume[4];
            ov_settings_row(p,2,0,offset,tab);ov_settings_row(p,2,OVM_ROWS-1,offset,resume);
            assert(tab[1]==a[1]&&resume[1]==b[1]);
            for(int row=1;row<OVM_ROWS-1;row++){
                if(!ov_settings_row(p,2,row,offset,r))continue;
                float y=fmaxf(r[1],v[1]),end=fminf(r[1]+r[3],v[1]+v[3]);
                if(end-y>0.01f)assert(ov_settings_hit(p,2,offset,p[0]+p[2]/2,(y+end)/2)==row);
                assert(fabsf(r[3]-a[3])<0.01f);
            }
            assert(ov_settings_hit(p,2,offset,p[0]+p[2]/2,a[1]+a[3]/2)==0);
            assert(ov_settings_hit(p,2,offset,p[0]+p[2]/2,b[1]+b[3]/2)==OVM_ROWS-1);
        }
        assert(ov_settings_reveal(2,0,7)==max);
        assert(ov_settings_reveal(2,max,1)==0);
        assert(ov_settings_reveal(2,max,OVM_ROWS-1)==max);
        assert(ov_settings_clamp(2,-100)==0&&ov_settings_clamp(2,100000)==max);
        ov_settings_row(p,0,1,0,r);assert(fabsf(r[3]-a[3])<0.01f);
        assert(ov_settings_hit(p,0,0,p[0]+p[2]/2,v[1]+v[3]*0.95f)==-1);
    }
    live=0;assert(ov_settings_count(2)==6);
    ov_menu_rect(1536,720,p);assert(!ov_settings_row(p,2,7,0,r));
    assert(ov_settings_gesture(1,0,0,3,60)==OV_SETTINGS_PENDING);
    assert(ov_settings_gesture(1,0,30,0,60)==OV_SETTINGS_SLIDER);
    assert(ov_settings_gesture(1,0,0,-30,60)==OV_SETTINGS_SCROLL);
    assert(ov_settings_gesture(0,0,0,30,60)==OV_SETTINGS_SCROLL);
    assert(ov_settings_gesture(1,OV_SETTINGS_SCROLL,100,0,60)==OV_SETTINGS_SCROLL);
    assert(ov_settings_gesture(1,OV_SETTINGS_SLIDER,0,100,60)==OV_SETTINGS_SLIDER);
#else
    ov_menu_rowrect(p,1,r);
    assert(r[3]>=p[3]*0.10f && "settings rows shrink to fit all controls");
#endif
    return 0;
}'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'test.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(base),str(p),'-lm','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)
if __name__=='__main__':unittest.main()
