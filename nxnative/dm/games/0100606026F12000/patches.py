# FINAL FANTASY RESONANCE (0100606026F12000): the rewriter's patches of this game's own builds. dm/tools/patch_nso.py loads every profile's patches.py and applies those of a module's NSO build ID;
# CUTS, OBS and FRAME_STEP as described there ("Registered patches"). Patches of Nintendo SDK module builds are in dm/tools/sdk_patches.py
CUTS = {
  # FINAL FANTASY RESONANCE 1.0.1 (v65536) main: Unreal Engine 5.6.1 constructs its FMallocBinned3 in place, as Octopath Traveler 0's: nninitStartup (main+0x13370) reserves the engine region
  # (62 GB unless -SwitchVirtualAddressSpaceSize= caps it, main+0x137d0), then calls the constructor on main+0x77c6210 (bss; main+0x137f0 → main+0xae770), which reserves 38 GB of address space
  # for its pools (main+0xae860). The call is cut; the host puts its allocator's vtable in the object before any guest code runs (hle_uemalloc.c). FPlatformMemory::BaseAllocator (main+0x13c64)
  # returns the object and the thread-local storage allocator (main+0x13830 / main+0x13870) calls its Malloc / Free slots; its pool base (main+0x7c148a0) and page size (main+0x7c14800)
  # are read only by the allocator's own code (main+0xae024 to main+0xb4568)
  '84e182f72ee1da4f477804bd05b54c15': [
    (0x137f0, 'e06b0294', '1f2003d5', 'nninitStartup: bl 0xae770 (FMallocBinned3 constructor, 38 GB of address space) → nop; the host allocator is in the object already (hle_uemalloc.c)'),
    # A failed lazy shader factory call used to return NULL immediately, even when a competing creator had already published the same shader.
    # Re-read the exact cache slot (resource +0x38, signed shader index w22) with acquire ordering, matching the callers' fast path.
    # A genuine miss remains NULL: never invent a shader, select an adjacent stage, or skip a rendering command. Reuse the unchanged ABI epilogue at 0x15686e8.
    (0x15686fc, 'f5031faae00315aaf44f43a9f65742a9', 'e81e40f908cd368b00fddfc8f8ffff17', 'lazy shader factory failure: acquire-load the competing publication before returning'),
  ],
}
