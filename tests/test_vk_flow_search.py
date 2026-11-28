"""Exercise the actual GLSL candidate-error function with deterministic texture taps."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class FlowSearch(unittest.TestCase):
    def test_bounded_error_preserves_decisions_and_reduces_reads(self):
        shader=(ROOT/'nxnative/dm/android/shaders/sgsr2_flow_convert.frag').read_text()
        start=shader.index('float errorAt(')
        function=shader[start:shader.index('\n}',start)+2].replace('0.2','0.2f')
        code=r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
struct vec2 {float x,y; vec2 operator+(vec2 o)const{return {x+o.x,y+o.y};}};
using sampler2D=const float*;
const vec2 taps[5]={{0,0},{1,0},{2,0},{3,0},{4,0}};
static unsigned reads;
static float luma(sampler2D image,vec2 p,vec2 inv){(void)inv;++reads;return image[(int)p.x];}
using std::abs;
static float referenceError(sampler2D image,vec2 center,vec2 invSize,float reference[5]) {
    float error=0.0f;
    for(int i=0;i<5;++i)error+=abs(reference[i]-luma(image,center+taps[i],invSize));
    return error*0.2f;
}
'''+function+r'''
int main(){
    uint32_t rng=1;
    auto next=[&](){rng=rng*1664525u+1013904223u;return float(rng>>8)/16777216.0f;};
    unsigned fullReads=0,boundedReads=0;
    for(unsigned n=0;n<100000;n++){
        float ref[5],candidate[5];
        for(int i=0;i<5;i++){ref[i]=next();candidate[i]=next();}
        float cost=next()*.1f;
        reads=0;float a=referenceError(candidate,{0,0},{1,1},ref);fullReads+=reads;
        reads=0;float b=errorAt(candidate,{0,0},{1,1},ref,cost,.001f);boundedReads+=reads;
        assert((a+.001f<cost)==(b+.001f<cost));
        b=errorAt(candidate,{0,0},{1,1},ref,cost,.005f);
        assert((a+.005f<cost)==(b+.005f<cost));
        float limit=cost+.006f;
        b=errorAt(candidate,{0,0},{1,1},ref,limit,0);
        assert((a<=limit)==(b<=limit));
        if(a<=cost)assert(a==errorAt(candidate,{0,0},{1,1},ref,cost,0));
    }
    float ref[5]={},candidate[5]={1,1,1,1,1};
    reads=0;assert(errorAt(candidate,{0,0},{1,1},ref,.03f,0)>.03f);assert(reads==1);
    float tie[5]={1,0,0,0,0};
    reads=0;assert(errorAt(tie,{0,0},{1,1},ref,.2f,0)==.2f);assert(reads==5);
    float nearTie[5]={.996f,0,0,0,0};
    reads=0;assert(!(errorAt(nearTie,{0,0},{1,1},ref,.2f,.001f)+.001f<.2f));assert(reads==1);
    nearTie[0]=.98f;
    reads=0;assert(!(errorAt(nearTie,{0,0},{1,1},ref,.2f,.005f)+.005f<.2f));assert(reads==1);
    assert(boundedReads<fullReads);
    printf("motion candidates: %u taps -> %u taps\n",fullReads,boundedReads);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'check.cpp';p.write_text(code)
            exe=p.with_suffix('')
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
