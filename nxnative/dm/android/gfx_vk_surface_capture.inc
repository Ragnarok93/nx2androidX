// Included by gfx_vk_exec.c. Full-frame capture uses one packed readback
// allocation and records every replay surface binding, even without pixels.
enum { SC_BYTES = 16 * 1024 * 1024, SC_TILE = 16, SC_ROWS = 128 * 1024, SC_SAMPLES = 8192 };
enum { SC_UNIFORM_PREFIX = 4096, SC_UNIFORM_BUDGET = 2 * 1024 * 1024 };
typedef struct {
    uint64_t present, submission, draws, dispatches, shader, image, view;
    uint64_t pool, write_sequence;
    int64_t offset, size;
    int pass, slot, stage, storage_format, view_format;
    unsigned element;
    uint32_t aspect, mip, mips, layer, layers, w, h, d, samples, usage;
    uint32_t nvn_format;
} surface_binding_t;
static struct {
    FILE *usage;
    char path[1024], temp[1024];
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *map;
    size_t used;
    unsigned rows, dropped, count;
    struct { size_t offset, size; } samples[SC_SAMPLES];
    size_t uniform_bytes;
    unsigned uniform_count;
    struct { VkDescriptorBufferInfo source; uint64_t shader; int binding, reg; } uniforms[96];
    unsigned storage_count;
    struct { const vkimg_t *im; surface_binding_t binding; } storage[96];
    unsigned alias_count;
    struct { vkimg_t image; surface_binding_t binding; const char *role; } aliases[32];
    int alloc_failed;
    // A direct-mapped cache suppresses identical descriptor bindings in the
    // same pass or dispatch. A collision emits the row instead of hiding it.
    surface_binding_t bindings[2048];
} g_sc;

static int surface_capture_arm(void) {
    const char *root = vk_pass_dump_root();
    memset(&g_sc, 0, sizeof g_sc);
    snprintf(g_sc.path, sizeof g_sc.path, "%s/surface_usage.tsv", root ? root : ".");
    snprintf(g_sc.temp, sizeof g_sc.temp, "%s/surface_usage.tsv.tmp", root ? root : ".");
    g_sc.usage = fopen(g_sc.temp, "w");
    if (!g_sc.usage) {
        VKLOG("[diagnostics] could not open full-frame surface catalog");
        return 0;
    }
    fputs("role\tpresent\tsubmission\tpass\tdraws\tdispatches\tslot\telement\tstage\tshader\timage\tview\tpool\twrite_sequence\tguest_offset\tguest_bytes\tnvn_format\tstorage_format\tview_format\taspect\tmip\tmips\tlayer\tlayers\twidth\theight\tdepth\tsamples\tusage\tload\tstore\tclear_bits\tstatus\n", g_sc.usage);
    return 1;
}

static surface_binding_t surface_binding(const vkimg_t *im, VkImageView view, VkFormat fmt,
    VkImageSubresourceRange range, int slot, int stage, uint64_t shader) {
    return (surface_binding_t){
        .present=g_tex_frame, .submission=g_set_epoch, .pass=g_sub_pass - 1,
        .draws=g_vkst.draws, .dispatches=g_vkst.dispatches, .shader=shader,
        .image=im ? (uint64_t)im->img : 0, .view=(uint64_t)view,
        .pool=im ? im->pool : 0, .write_sequence=im ? im->wseq : 0, .offset=im ? im->off : 0, .size=im ? im->size : 0,
        .slot=slot, .stage=stage, .storage_format=im ? im->fmt : 0, .view_format=fmt,
        .aspect=range.aspectMask, .mip=range.baseMipLevel, .mips=range.levelCount,
        .layer=range.baseArrayLayer, .layers=range.layerCount,
        .w=im ? im->w : 0, .h=im ? im->h : 0, .d=im ? im->d : 0,
        .samples=im ? im->samples : 0, .usage=im ? im->usage : 0,
        .nvn_format=im ? (uint32_t)im->nvn_format : 0,
    };
}

static void surface_usage(const char *role, const surface_binding_t *b, int load, int store,
    const VkClearValue *clear, const char *status) {
    if (!g_sc.usage) return;
    if (g_sc.rows >= SC_ROWS) { g_sc.dropped++; return; }
    uint32_t bits[4] = {0};
    if (clear) memcpy(bits, clear, sizeof bits);
    if (fprintf(g_sc.usage, "%s\t%llu\t%llu\t%d\t%llu\t%llu\t%d\t%u\t%d\t%016llx\t%llx\t%llx\t%llx\t%llu\t%lld\t%lld\t%x\t%d\t%d\t%x\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%x\t%d\t%d\t%08x,%08x,%08x,%08x\t%s\n",
        role, (unsigned long long)b->present, (unsigned long long)b->submission, b->pass,
        (unsigned long long)b->draws, (unsigned long long)b->dispatches, b->slot, b->element, b->stage,
        (unsigned long long)b->shader, (unsigned long long)b->image, (unsigned long long)b->view,
        (unsigned long long)b->pool, (unsigned long long)b->write_sequence, (long long)b->offset, (long long)b->size, b->nvn_format,
        b->storage_format, b->view_format, b->aspect, b->mip, b->mips, b->layer, b->layers,
        b->w, b->h, b->d, b->samples, b->usage, load, store, bits[0], bits[1], bits[2], bits[3], status) >= 0)
        g_sc.rows++;
}

static void surface_descriptor(const vkimg_t *im, VkImageView view, int st, int binding, unsigned element, int storage) {
    surface_binding_t b = surface_binding(im, view, view ? vk_view_fmt : VK_FORMAT_UNDEFINED,
        view ? vk_view_range : (VkImageSubresourceRange){0}, binding, st, S.sh[st] ? S.sh[st]->key : 0);
    // Bindings can be repeated hundreds of times within the same pass. Keep
    // the first draw ordinal in the output and key on the rest of the tuple.
    b.element = element;
    if (st != 5 && S.pass_open == 2) b.pass++;
    if (st == 5 && storage && view) {
        if (g_sc.storage_count < sizeof g_sc.storage / sizeof g_sc.storage[0]) {
            unsigned i = g_sc.storage_count++;
            g_sc.storage[i].im = im;
            g_sc.storage[i].binding = b;
        } else {
            surface_usage("compute-image", &b, -1, -1, NULL, "binding-limit");
        }
    }
    uint64_t draw = b.draws;
    b.draws = (uint64_t)storage;
    unsigned hash = (unsigned)(b.view ^ b.shader ^ (uint64_t)b.pass * 31 ^ (uint64_t)binding * 257 ^ element * 13u ^ (unsigned)storage);
    surface_binding_t *previous = &g_sc.bindings[hash % (sizeof g_sc.bindings / sizeof g_sc.bindings[0])];
    if (previous->present && !memcmp(previous, &b, sizeof b)) return;
    // Normalize the role for comparison in the cache while keeping the draw
    // ordinal in the written row.
    *previous = b;
    b.draws = draw;
    surface_usage(storage ? "storage-binding" : "sampled-binding", &b, -1, -1, NULL,
                  view ? "metadata" : "dummy-binding");
}

static void surface_texel_buffer(const tex_t *t, VkBufferView view, int st, int binding, int storage, int snapshot) {
    const vkfmt_t *format = t ? vk_fmt(t->d.format) : NULL;
    surface_binding_t b = surface_binding(NULL, VK_NULL_HANDLE, format ? format->vk : VK_FORMAT_UNDEFINED,
        (VkImageSubresourceRange){0}, binding, st, S.sh[st] ? S.sh[st]->key : 0);
    if (st != 5 && S.pass_open == 2) b.pass++;
    b.view = (uint64_t)view;
    if (t) {
        b.pool = (uint64_t)(uintptr_t)t->pool;
        b.offset = t->off;
        b.size = t->L.size;
        b.w = t->d.width;
        b.nvn_format = t->d.format;
    }
    surface_usage(storage ? "storage-texel-buffer" : "sampled-texel-buffer", &b, -1, -1,
                  NULL, view ? (snapshot ? "gpu-snapshot" : "direct-view") : "dummy-binding");
}

static void surface_capture_free(void) {
    if (g_sc.buffer) vkDestroyBuffer(V.dev, g_sc.buffer, NULL);
    if (g_sc.memory) vkFreeMemory(V.dev, g_sc.memory, NULL);
}

static void surface_capture_abort(void) {
    if (g_sc.usage) {
        fclose(g_sc.usage);
        unlink(g_sc.temp);
    }
    surface_capture_free();
    memset(&g_sc, 0, sizeof g_sc);
}

static int surface_capture_allocate(void) {
    if (g_sc.buffer) return 1;
    if (g_sc.alloc_failed) return 0;
    VkBufferCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0,
        SC_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE};
    VkResult r = vkCreateBuffer(V.dev, &ci, NULL, &g_sc.buffer);
    if (r != VK_SUCCESS) goto failed;
    VkMemoryRequirements rq;
    vkGetBufferMemoryRequirements(V.dev, g_sc.buffer, &rq);
    uint32_t type = vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
    if (type == UINT32_MAX) type = vk_mem_type(rq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0);
    if (type == UINT32_MAX || !vk_pd_budget_allows(g_pd_bytes, rq.size, pass_dump_budget())) goto failed;
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, rq.size, type};
    if (vkAllocateMemory(V.dev, &ai, NULL, &g_sc.memory) != VK_SUCCESS ||
        vkBindBufferMemory(V.dev, g_sc.buffer, g_sc.memory, 0) != VK_SUCCESS ||
        vkMapMemory(V.dev, g_sc.memory, 0, VK_WHOLE_SIZE, 0, &g_sc.map) != VK_SUCCESS) goto failed;
    g_pd_bytes += rq.size;
    return 1;
failed:
    surface_capture_free();
    g_sc.buffer = VK_NULL_HANDLE;
    g_sc.memory = VK_NULL_HANDLE;
    g_sc.alloc_failed = 1;
    VKLOG("[diagnostics] surface readback allocation unavailable");
    return 0;
}

static void surface_uniform_queue(uint64_t shader, int binding, int reg, VkDescriptorBufferInfo source) {
    if (g_sc.uniform_count >= sizeof g_sc.uniforms / sizeof g_sc.uniforms[0]) return;
    unsigned i = g_sc.uniform_count++;
    g_sc.uniforms[i].source = source;
    g_sc.uniforms[i].shader = shader;
    g_sc.uniforms[i].binding = binding;
    g_sc.uniforms[i].reg = reg;
}

// Snapshot the effective descriptor before dispatch, including any uniform
// shadow produced by replay. The dispatch may itself change the original bytes.
static void surface_compute_uniforms(VkCommandBuffer cb) {
    int copied = 0;
    for (unsigned i = 0; i < g_sc.uniform_count; i++) {
        const VkDescriptorBufferInfo *src = &g_sc.uniforms[i].source;
        surface_binding_t b = surface_binding(NULL, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED,
            (VkImageSubresourceRange){0}, g_sc.uniforms[i].binding, 5, g_sc.uniforms[i].shader);
        b.element = (unsigned)g_sc.uniforms[i].reg;
        b.size = (int64_t)src->range;
        size_t bytes = src->range < SC_UNIFORM_PREFIX ? (size_t)src->range : SC_UNIFORM_PREFIX;
        bytes &= ~(size_t)3;
        size_t at = (g_sc.used + 3) & ~(size_t)3;
        if (!src->buffer || src->offset % 4 || !bytes) {
            surface_usage("compute-uniform", &b, -1, -1, NULL, "unavailable");
            continue;
        }
        if (at > SC_BYTES || bytes > SC_BYTES - at ||
            bytes > SC_UNIFORM_BUDGET - g_sc.uniform_bytes ||
            g_sc.rows >= SC_ROWS || g_sc.count >= SC_SAMPLES) {
            surface_usage("compute-uniform", &b, -1, -1, NULL, "readback-budget");
            continue;
        }
        if (!surface_capture_allocate()) {
            surface_usage("compute-uniform", &b, -1, -1, NULL, "readback-unavailable");
            break;
        }
        if (!copied) vk_img_barrier_all(cb);
        copied = 1;
        VkBufferCopy copy = {src->offset, at, bytes};
        vkCmdCopyBuffer(cb, src->buffer, g_sc.buffer, 1, &copy);
        g_sc.samples[g_sc.count].offset = at;
        g_sc.samples[g_sc.count++].size = bytes;
        g_sc.used = at + bytes;
        g_sc.uniform_bytes += bytes;
        char sample[80];
        snprintf(sample, sizeof sample, "ubo:%zu:%zu", at, bytes);
        surface_usage("compute-uniform", &b, -1, -1, NULL, sample);
    }
    g_sc.uniform_count = 0;
    if (copied) {
        VkMemoryBarrier host = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host, 0, NULL, 0, NULL);
    }
}

static unsigned surface_block_bytes(const vkimg_t *im, VkImageAspectFlags aspect,
    unsigned *bw, unsigned *bh) {
    *bw = *bh = 1;
    if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) return 1;
    if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
        switch (im->fmt) {
        case VK_FORMAT_D16_UNORM: case VK_FORMAT_D16_UNORM_S8_UINT: return 2;
        case VK_FORMAT_X8_D24_UNORM_PACK32: case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT: case VK_FORMAT_D32_SFLOAT_S8_UINT: return 4;
        default: return 0;
        }
    }
    if (aspect != VK_IMAGE_ASPECT_COLOR_BIT) return 0;
    const vkfmt_t *f = vk_fmt(im->nvn_format);
    if (!f) return 0;
    if (f->dec) return f->dbpp;
    *bw = f->bw ? f->bw : 1;
    *bh = f->bh ? f->bh : 1;
    return f->bpb;
}

// The TSV stores the offsets which will be valid once surface_samples.raw is
// published. Sample every used layer, including depth slices of 3D attachments.
static void surface_read(const char *role, const vkimg_t *im, surface_binding_t b,
    const VkRenderingAttachmentInfo *att, const char *reason) {
    if (!g_sc.usage) return;
    int load = att ? (int)att->loadOp : -1, store = att ? (int)att->storeOp : -1;
    const VkClearValue *clear = att && att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? &att->clearValue : NULL;
    unsigned bw = 1, bh = 1, bpb = 0;
    if (!reason && !im) reason = "unresolved";
    if (!reason && att && att->storeOp != VK_ATTACHMENT_STORE_OP_STORE &&
        att->storeOp != VK_ATTACHMENT_STORE_OP_NONE) reason = "discarded";
    if (!reason && im->samples != 1) reason = "multisampled";
    if (!reason && (!(im->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
                     im->layout != VK_IMAGE_LAYOUT_GENERAL)) reason = "transfer-ineligible";
    if (!reason && im->rs_backing &&
        (im->rs_backing->physical_w != im->w || im->rs_backing->physical_h != im->h))
        reason = "scaled-backing";
    if (!reason && (!b.layers || b.mip >= im->levels)) reason = "invalid-range";
    if (!reason && !(bpb = surface_block_bytes(im, b.aspect, &bw, &bh))) reason = "unsupported-format";
    if (reason) {
        surface_usage(role, &b, load, store, clear, reason);
        return;
    }

    unsigned mw = im->w >> b.mip, mh = im->h >> b.mip;
    if (!mw) mw = 1;
    if (!mh) mh = 1;
    unsigned w = mw > SC_TILE ? SC_TILE / bw * bw : mw;
    unsigned h = mh > SC_TILE ? SC_TILE / bh * bh : mh;
    unsigned x = (mw - w) / 2 / bw * bw, y = (mh - h) / 2 / bh * bh;
    size_t bytes = (size_t)((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bpb;
    size_t alignment = 4u * bpb;
    int is3d = im->vtype == VK_IMAGE_VIEW_TYPE_3D;
    uint32_t available = is3d ? im->d >> b.mip : im->layers;
    if (!available) available = 1;
    if (b.layer >= available || b.layers > available - b.layer) {
        surface_usage(role, &b, load, store, clear, "invalid-range");
        return;
    }
    unsigned end = b.layer + b.layers;
    b.layers = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    for (; b.layer < end; b.layer++) {
        size_t at = (g_sc.used + alignment - 1) / alignment * alignment;
        if (at > SC_BYTES || bytes > SC_BYTES - at || g_sc.rows >= SC_ROWS || g_sc.count >= SC_SAMPLES) {
            b.layers = end - b.layer;
            surface_usage(role, &b, load, store, clear, "readback-budget");
            break;
        }
        if (!surface_capture_allocate()) {
            b.layers = end - b.layer;
            surface_usage(role, &b, load, store, clear, "readback-unavailable");
            break;
        }
        if (!cb) {
            cb = vk_cmd();
            vk_img_barrier_all(cb);
        }
        VkBufferImageCopy copy = {at, 0, 0,
            {b.aspect, b.mip, is3d ? 0 : b.layer, 1},
            {(int32_t)x, (int32_t)y, is3d ? (int32_t)b.layer : 0}, {w, h, 1}};
        vkCmdCopyImageToBuffer(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, g_sc.buffer, 1, &copy);
        g_sc.samples[g_sc.count].offset = at;
        g_sc.samples[g_sc.count++].size = bytes;
        g_sc.used = at + bytes;
        char sample[128];
        snprintf(sample, sizeof sample, "tile:%zu:%zu:%u:%u:%u:%u", at, bytes, x, y, w, h);
        surface_usage(role, &b, load, store, clear, sample);
    }
    if (cb) {
        VkMemoryBarrier host = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &host, 0, NULL, 0, NULL);
    }
}

static void surface_alias_add(const char *role, const vkimg_t *im, VkImageView view,
                              VkFormat fmt, VkImageSubresourceRange range,
                              int st, int binding, uint64_t key) {
    surface_binding_t b = surface_binding(im, view, fmt, range, binding, st, key);
    if (g_sc.alias_count == sizeof g_sc.aliases / sizeof g_sc.aliases[0]) {
        surface_usage(role, &b, -1, -1, NULL, "binding-limit");
        return;
    }
    unsigned i = g_sc.alias_count++;
    // Deferred deletion keeps the VkImage live for this submission even if
    // a guest pool retires before the pass closes.
    g_sc.aliases[i].image = *im;
    g_sc.aliases[i].binding = b;
    g_sc.aliases[i].role = role;
}

static void surface_alias_flush(void) {
    for (unsigned i = 0; i < g_sc.alias_count; i++) {
        surface_binding_t b = g_sc.aliases[i].binding;
        // A single representative layer and mip keeps these optional
        // candidate samples from taking the scene attachment budget.
        if (b.layers) b.layers = 1;
        const vkimg_t *im = &g_sc.aliases[i].image;
        surface_read(g_sc.aliases[i].role, im, b, NULL,
                     b.aspect && !(b.aspect & (b.aspect - 1)) ? NULL : "unsupported-aspect");
    }
    g_sc.alias_count = 0;
}

static void surface_dispatch(void) {
    for (unsigned i = 0; i < g_sc.storage_count; i++) {
        surface_binding_t b = g_sc.storage[i].binding;
        // A storage image uses a single mip in the shader; preserve the
        // bound view in the binding row even if it contains other mips.
        b.mips = 1;
        surface_read("compute-image", g_sc.storage[i].im, b, NULL, NULL);
    }
}
static void surface_pass(void) {
    surface_binding_t pass = surface_binding(NULL, VK_NULL_HANDLE, VK_FORMAT_UNDEFINED,
        (VkImageSubresourceRange){0}, -1, -1, 0);
    pass.w = P_ri.renderArea.extent.width;
    pass.h = P_ri.renderArea.extent.height;
    pass.layers = P_ri.layerCount;
    pass.samples = S.samples;
    surface_usage("render-area", &pass, -1, -1, NULL, S.rp_pass ? "render-pass" : "dynamic-rendering");
    for (int i = 0; i < 9; i++) {
        if (i < 8 ? !S.rt_has_col[i] && !P_att[i].im : !S.rt_has_dep) continue;
        const vkimg_t *im = P_att[i].im;
        const char *status = i < 8 && P_alias[i] != i ? "attachment-alias" : NULL;
        char alias[32];
        if (status) {
            snprintf(alias, sizeof alias, "attachment-alias:%u", (unsigned)P_alias[i]);
            status = alias;
        }
        VkImageSubresourceRange range = P_att[i].range;
        if (range.layerCount > P_ri.layerCount) range.layerCount = P_ri.layerCount;
        surface_binding_t b = surface_binding(im, i < 8 ? P_ca[i].imageView : P_da.imageView,
            im ? P_att[i].fmt : VK_FORMAT_UNDEFINED, range, i, -1, 0);
        if (i < 8) {
            b.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            surface_read("color", im, b, &P_ca[i], status);
        } else {
            if (!im || P_ri.pDepthAttachment) {
                b.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                surface_read("depth", im, b, &P_da, NULL);
            }
            if (P_ri.pStencilAttachment) {
                b.aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
                surface_read("stencil", im, b, &P_sa, NULL);
            }
        }
    }
}

static void surface_capture_write(void) {
    if (!g_sc.usage) return;
    const char *root = vk_pass_dump_root();
    char raw[1024], raw_tmp[1024], index[1024], index_tmp[1024];
    snprintf(raw, sizeof raw, "%s/surface_samples.raw", root ? root : ".");
    snprintf(raw_tmp, sizeof raw_tmp, "%s/surface_samples.raw.tmp", root ? root : ".");
    snprintf(index, sizeof index, "%s/surface_samples.tsv", root ? root : ".");
    snprintf(index_tmp, sizeof index_tmp, "%s/surface_samples.tsv.tmp", root ? root : ".");
    VkMappedMemoryRange inv = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, g_sc.memory, 0, VK_WHOLE_SIZE};
    int ok = g_sc.count && vkInvalidateMappedMemoryRanges(V.dev, 1, &inv) == VK_SUCCESS;
    FILE *rf = ok ? fopen(raw_tmp, "wb") : NULL, *ix = ok ? fopen(index_tmp, "w") : NULL;
    ok = rf && ix;
    size_t pos = 0;
    if (ok) ok = fputs("offset\tbytes\tfnv1a64\n", ix) >= 0;
    for (unsigned i = 0; ok && i < g_sc.count; i++) {
        size_t at = g_sc.samples[i].offset, bytes = g_sc.samples[i].size;
        const uint8_t *p = (const uint8_t *)g_sc.map + at;
        while (ok && pos < at) { ok = fputc(0, rf) != EOF; pos++; }
        ok = ok && fwrite(p, 1, bytes, rf) == bytes &&
             fprintf(ix, "%zu\t%zu\t%016llx\n", at, bytes, (unsigned long long)vk_pd_hash(p, bytes)) >= 0;
        pos = at + bytes;
    }
    if (rf && fclose(rf) != 0) ok = 0;
    if (ix && fclose(ix) != 0) ok = 0;
    if (ok) ok = rename(raw_tmp, raw) == 0 && rename(index_tmp, index) == 0;
    if (!ok) {
        unlink(raw_tmp); unlink(index_tmp);
        unlink(raw); unlink(index);
    }
    int note = fprintf(g_sc.usage, "# rows=%u dropped_rows=%u completed_samples=%u\n",
                      g_sc.rows, g_sc.dropped, ok ? g_sc.count : 0);
    int closed = fclose(g_sc.usage);
    if (note < 0 || closed != 0 || rename(g_sc.temp, g_sc.path) != 0) unlink(g_sc.temp);
    g_sc.usage = NULL;
    surface_capture_free();
}

static void surface_transfer(const vkimg_t *im, VkImageSubresourceLayers sub, const char *role) {
    VkImageSubresourceRange range = {sub.aspectMask, sub.mipLevel, 1, sub.baseArrayLayer, sub.layerCount};
    surface_binding_t b = surface_binding(im, VK_NULL_HANDLE, im->fmt, range, -1, -1, 0);
    surface_usage(role, &b, -1, -1, NULL, "requested");
}

static void surface_clear(const vkimg_t *im, VkImageSubresourceRange range, const VkClearValue *clear) {
    surface_binding_t b = surface_binding(im, VK_NULL_HANDLE, im->fmt, range, -1, -1, 0);
    surface_usage("texture-clear", &b, -1, -1, clear, "cleared");
    if (im->vtype == VK_IMAGE_VIEW_TYPE_3D)
        b.layers = im->d >> b.mip ? im->d >> b.mip : 1;
    static const VkImageAspectFlags aspects[] = {
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_ASPECT_STENCIL_BIT
    };
    for (unsigned i = 0; i < sizeof aspects / sizeof aspects[0]; i++) {
        if (!(range.aspectMask & aspects[i])) continue;
        b.aspect = aspects[i];
        surface_read("texture-clear-output", im, b, NULL, NULL);
    }
}
