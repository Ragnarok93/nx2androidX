"""Prepare Qualcomm's pinned shaders for Vulkan, leaving the vendor import intact."""
import re


VARIANTS = {
    "2fs": ("glsl_2_pass_fs", {"convert": "fs", "upscale": "fs"}),
    "2cs": ("glsl_2_pass_cs", {"convert": "comp", "upscale": "comp"}),
    "3cs": ("glsl_3_pass_cs", {"convert": "comp", "activate": "comp", "upscale": "comp"}),
}


def convert_inputs(text, source, variant):
    """Fold depth and velocity normalization into the vendor's first pass."""
    text = text.replace("} params;", "    highp vec4 inputConvention;\n} params;", 1)
    text = text.replace("uniform mediump sampler2D InputDepth", "uniform highp sampler2D InputDepth")
    text = text.replace("uniform mediump sampler2D InputVelocity", "uniform highp sampler2D InputVelocity")
    text, gathers = re.subn(r"textureGather\(InputDepth,\s*(.*),\s*0\)",
                           r"gsr2GatherDepth(\1)", text)
    if gathers != (3 if variant == "3cs" else 4):
        raise ValueError(f"{source}: unexpected depth-gather count")
    if variant == "3cs":
        old = "texelFetch(InputDepth, InputPosBtmRight, 0).x"
        new = ("gsr2Depth(texelFetch(InputDepth, min(InputPosBtmRight, "
               "ivec2(params.renderSize) - ivec2(1)), 0).x)")
        if old not in text:
            raise ValueError(f"{source}: missing depth neighbor fetch")
        text = text.replace(old, new)
    helpers = """
float gsr2Depth(float depth) {
    return params.inputConvention.z > 0.0 ? 1.0 - depth : depth;
}

vec4 gsr2GatherDepth(vec2 coord) {
    vec4 depth = textureGather(InputDepth, coord, 0);
    return params.inputConvention.z > 0.0 ? vec4(1.0) - depth : depth;
}

"""
    text = text.replace("void main()", helpers + "void main()", 1)
    old = "if (EncodedVelocity.x > 0.0)"
    decode = "motion = decodeVelocityFromTexture(EncodedVelocity.xy);"
    position = "        vec3 Position = vec3(ScreenPos, "
    if not all(expression in text for expression in (old, decode, position)):
        raise ValueError(f"{source}: unexpected motion reprojection")
    text = text.replace(old, """bool signedMotion = params.inputConvention.w > 0.0;
    if (signedMotion ? EncodedVelocity.a > 0.0 : EncodedVelocity.x > 0.0)""")
    text = text.replace(decode, """vec2 velocity = signedMotion ? EncodedVelocity.xy : decodeVelocityFromTexture(EncodedVelocity.xy);
        motion = velocity * params.inputConvention.xy;""")
    # ScreenPos uses Y-up clip space, while jitterOffset is measured from
    # the top-left in render pixels. Remove it before camera reprojection.
    text = text.replace(position, """        ScreenPos -= params.jitterOffset * vec2(2.0, -2.0) / vec2(params.renderSize);
""" + position)
    return text


def shader_sources(vendor, generated):
    """Create named, stage-suffixed copies for glslc and return their paths."""
    generated.mkdir(parents=True, exist_ok=True)
    shaders = []
    for variant, (directory, stages) in VARIANTS.items():
        for stage, extension in stages.items():
            source = vendor / directory / f"sgsr2_{stage}.{extension}"
            text = source.read_text(encoding="utf-8")
            output_stage = extension if extension == "comp" else "frag"
            if variant == "2cs":
                # The reference declares its GL image slots separately from
                # textures and UBOs. Vulkan uses one binding namespace per set.
                text, remapped = re.subn(r"layout\(binding = ([01]), (rgba16f|r32ui)\)",
                    lambda m: f"layout(set = 0, binding = {int(m[1]) + 4}, {m[2]})", text)
                if remapped != 2:
                    raise ValueError(f"{source}: expected two GL image bindings")
            if stage == "convert":
                text = convert_inputs(text, source, variant)
            if extension == "comp":
                # A real render size is not necessarily a multiple of eight.
                size = "displaySize" if stage == "upscale" else "renderSize"
                replacement = ("void main()\n{\n"
                               f"    if (any(greaterThanEqual(gl_GlobalInvocationID.xy, params.{size}))) return;")
                text, inserted = re.subn(r"void main\(\)\s*\{", replacement, text)
                if inserted != 1:
                    raise ValueError(f"{source}: unable to guard compute dispatch extent")
            # All runtime inputs use top-left image UV and positive-up clip Y.
            text = text.replace("#version 320 es", "#version 320 es\n#define REQUEST_NDC_Y_UP 1", 1)
            path = generated / f"sgsr2_{variant}_{stage}.{output_stage}"
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                path.write_text(text, encoding="utf-8", newline="\n")
            shaders.append(path)
    return shaders
