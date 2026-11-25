"""Prepare Qualcomm's pinned shaders for Vulkan, leaving the vendor import intact."""
import re


VARIANTS = {
    "2fs": ("glsl_2_pass_fs", {"convert": "fs", "upscale": "fs"}),
    "2cs": ("glsl_2_pass_cs", {"convert": "comp", "upscale": "comp"}),
    "3cs": ("glsl_3_pass_cs", {"convert": "comp", "activate": "comp", "upscale": "comp"}),
}


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
