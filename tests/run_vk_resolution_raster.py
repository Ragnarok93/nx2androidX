"""Run real Vulkan allocation, render-area/scissor and native-consumer readback."""
from pathlib import Path
import os
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
VK=ROOT/"nxnative/dm/android"
NVN=ROOT/"nxnative/dm/gfx/nvn"

def cut(path,start,end):
    text=path.read_text()
    i=text.index(start)
    return text[i:text.index(end,i)]

SHADERS={
"producer.vert":"""#version 450
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.0-1.0,0.25,1);}
""",
"producer.frag":"""#version 450
layout(push_constant) uniform PC {float scale;} pc;
layout(location=0) out vec4 color;
void main(){color=gl_FragCoord.x/pc.scale<692.0?vec4(1,0,0,1):vec4(0,1,0,1);}
""",
"consumer.frag":"""#version 450
layout(set=0,binding=0) uniform sampler2D source;
layout(push_constant) uniform PC {float scale;} pc;
layout(location=0) out vec4 color;
void main(){color=texelFetch(source,ivec2(gl_FragCoord.xy*vec2(textureSize(source,0))/vec2(1920,1080)),0);}
"""
}
def main():
    with tempfile.TemporaryDirectory(prefix="nx2-raster-") as tmp:
        b=Path(tmp)
        # Compile the actual allocator and replay geometry functions rather
        # than a parallel implementation. Only Android context is supplied
        # by the harness.
        types=cut(VK/"gfx_vk.h","typedef struct vkimg vkimg_t;","// Replay thread, outside any render pass.")
        (b/"rs_types.inc").write_text(types)
        allocator=cut(VK/"gfx_vk_tex.c","static nx_rs_budget g_rs_budget,","// A private storage control")
        destroy=cut(VK/"gfx_vk_tex.c","void vk_rs_variant_destroy(vk_rs_variant *v) {","void vk_rs_variant_clear(")
        memtype=cut(VK/"gfx_vk_core.c","uint32_t vk_mem_type(","static void mem_diag_pool(")
        (b/"rs_alloc.inc").write_text(memtype+"\nvoid vk_rs_variant_destroy(vk_rs_variant *v);\n"+allocator+destroy)
        raster=cut(VK/"gfx_vk_exec.c","static int resolution_configure_pass(","// Attachment preflight")
        scissor=cut(VK/"gfx_vk_exec.c","static VkRect2D scissor_rect(","// A clear of some channels only")
        (b/"rs_raster.inc").write_text(raster+scissor)
        for name,src in SHADERS.items():
            path=b/name;path.write_text(src);spv=b/(name+".spv")
            subprocess.run(["glslc","--target-env=vulkan1.1","-O",path,"-o",spv],check=True)
            subprocess.run(["spirv-val","--target-env","vulkan1.1",spv],check=True)
            words=", ".join(hex(w[0]) for w in struct.iter_unpack("<I",spv.read_bytes()))
            (b/(name+".h")).write_text("static const uint32_t "+name.replace(".","_")+"[]={"+words+"};\n")
        exe=b/"resolution-raster"
        subprocess.run([os.environ.get("CC","cc"),"-std=c11","-O2","-Wall","-Wextra","-Wno-missing-field-initializers","-I",str(b),"-I",str(NVN),"-I",str(NVN/"include"),"-I",str(ROOT/"nxnative/dm/gfx/tex"),ROOT/"tests/vk_resolution_raster.c","-lvulkan","-lm","-o",exe],check=True)
        r=subprocess.run([exe],capture_output=True,text=True)
        print(r.stdout,end="");print(r.stderr,end="")
        return 1 if "Validation Error" in r.stdout+r.stderr else r.returncode

if __name__=="__main__":
    raise SystemExit(main())
