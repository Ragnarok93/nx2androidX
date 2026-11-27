"""The existing read-only scaling status must live exclusively in Diagnostics."""
from pathlib import Path
import subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
class RecordedScale(unittest.TestCase):
    def test_diagnostics_only(self):
        code=(ROOT/'nxnative/dm/android/overlay_layout.h').read_text()
        a=code.index('enum { OVM_OVERLAY,'); b=code.index('#define OV_MENU_ROW',a)
        source='''#include <assert.h>\n#define __ANDROID__ 1\n'''+code[a:b]+r'''
int dm_menu_audio_available(void){return 1;}
static int live=1;
int dm_menu_rs_live(void){return live;}
int main(void){
    assert(OVM_TAB_NAMES[2][0]=='G');
    for(live=0;live<=1;live++){
        int count=0;
        for(int page=0;page<OVM_PAGES;page++)for(int row=1;row<OVM_ROWS-1;row++){
            if(ov_menu_item(page,row)==OVM_DRS_NOW){
                assert(page==OVM_DIAG_TAB && "Recorded scale must be on Diagnostics only");
                assert(ov_menu_item(page,row+1)==OVM_DIAG_BACK);
                ++count;
            }
        }
        assert(count==1);
        assert(ov_menu_item(2,2)==OVM_FSR && ov_menu_item(2,3)==OVM_FSR_SHARP);
    }
    return 0;
}'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'test.c';p.write_text(source);exe=p.with_suffix('')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,cwd=d)
if __name__=='__main__':unittest.main()
