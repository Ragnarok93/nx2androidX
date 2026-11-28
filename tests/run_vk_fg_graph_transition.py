"""Run real LSFG graph construction and handoff against software Vulkan.
Synthetic compute modules exercise the actual resource and command path;
the generator mixes previous/current inputs rather than proprietary LSFG.
"""
from pathlib import Path
import os,re,struct,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
ANDROID=ROOT/'nxnative/dm/android'
NOOP='#version 450\nlayout(local_size_x=1) in; void main() {}'
GEN="""#version 450
layout(local_size_x=16,local_size_y=16) in;
layout(set=0,binding=2) uniform sampler sampleState;
layout(set=0,binding=3) uniform texture2D previous;
layout(set=0,binding=4) uniform texture2D current;
layout(set=0,binding=8,rgba8) uniform writeonly image2D outputImage;
void main(){
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(p,imageSize(outputImage))))return;
    imageStore(outputImage,p,(texelFetch(sampler2D(previous,sampleState),p,0)+texelFetch(sampler2D(current,sampleState),p,0))*.5);
}
"""
def main():
    with tempfile.TemporaryDirectory(prefix='nx2-fg-transition-') as tmp:
        b=Path(tmp)
        shim=(ANDROID/'vka.h').read_text()
        shim=shim.replace('#include "gfx_vk.h"','').replace('#include "dm.h"','')
        (b/'fg_vka_shim.h').write_text(shim)
        entries=re.findall(r'PFN_(vk\w+)\s+(vk\w+)\s*;',shim)
        assert entries and all(a==c for a,c in entries)
        (b/'fg_vka_entries.h').write_text('\n'.join(f'.{a}={a},' for a,_ in entries))
        modules=[]
        for name,source in [('noop',NOOP),('gen',GEN)]:
            p=b/(name+'.comp');p.write_text(source);spv=b/(name+'.spv')
            subprocess.run(['glslc','--target-env=vulkan1.1',p,'-o',spv],check=True)
            subprocess.run(['spirv-val','--target-env','vulkan1.1',spv],check=True)
            words=', '.join(hex(v[0]) for v in struct.iter_unpack('<I',spv.read_bytes()))
            modules.append(f'static const uint32_t {name}_spv[]={{ {words} }};')
        (b/'fg_shader_modules.h').write_text('\n'.join(modules))
        exe=b/'fg-probe'
        subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O2','-pthread',
            '-I',str(ANDROID),'-I',str(b),str(ROOT/'tests/vk_fg_graph_transition.c'),'-lvulkan','-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],text=True,capture_output=True,timeout=120)
        print(result.stdout,end='');print(result.stderr,end='')
        return 1 if result.returncode or re.search(r'Validation Error|SYNC-HAZARD',result.stdout+result.stderr) else 0
if __name__=='__main__':raise SystemExit(main())
