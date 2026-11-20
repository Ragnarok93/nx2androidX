// Included by gfx_vk_exec.c. Run only during an armed diagnostic capture.
// Copy at the natural end of the render pass, before any attachment writeback
// or later dispatch. A pass with graphics memory writes or transform feedback
// cannot establish that its end-of-pass bytes match its draws' input bytes.
enum { INPUT_MAX = 96, INPUT_BYTES = 256, INPUT_PER_PASS = 4 };
typedef struct {
    VkBuffer buffer;
    VkDeviceSize offset;
    uint64_t address, pool, pool_offset, pool_serial, binding_extent;
    uint64_t present, submission, draw, dispatches, vs, fs, gs;
    unsigned size, stride, count, divisor, attr_count;
    struct { uint32_t location, format, offset; } attr[16];
    int pass, index_type, stream;
    const char *kind;
} input_sample_t;
static input_sample_t g_inputs[INPUT_MAX];
static unsigned g_input_count, g_input_pending, g_input_draws, g_input_rejected;
static int g_input_capture, g_input_alloc_failed;
static uint64_t g_input_vs;
static VkBuffer g_input_buffer;
static VkDeviceMemory g_input_memory;
static void *g_input_map;

static void input_capture_path(char path[1024], const char *suffix) {
    const char *root = getenv("DM_ROOT");
    snprintf(path, 1024, "%s/gpu_draw_inputs.tsv%s", root ? root : ".", suffix);
}

static void input_capture_arm(void) {
    const char *enable = getenv("DM_VK_PASSDUMP_INPUTS");
    const char *shader = getenv("DM_VK_PASSDUMP_INPUT_VS");
    g_input_capture = enable && atoi(enable);
    g_input_vs = shader ? strtoull(shader, NULL, 16) : 0;
    g_input_count = g_input_pending = g_input_draws = g_input_rejected = 0;
    g_input_alloc_failed = 0;
    char path[1024];
    input_capture_path(path, "");
    unlink(path);
}

static void input_capture_free(void) {
    if (g_input_buffer) vkDestroyBuffer(V.dev, g_input_buffer, NULL);
    if (g_input_memory) vkFreeMemory(V.dev, g_input_memory, NULL); // also unmaps
    g_input_map = NULL;
    g_input_buffer = VK_NULL_HANDLE;
    g_input_memory = VK_NULL_HANDLE;
}

static int input_capture_allocate(void) {
    if (g_input_buffer) return 1;
    if (g_input_alloc_failed) return 0;
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0,
        INPUT_MAX * INPUT_BYTES, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE};
    VkResult r = vkCreateBuffer(V.dev, &bi, NULL, &g_input_buffer);
    if (r != VK_SUCCESS) goto fail;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(V.dev, g_input_buffer, &req);
    uint32_t type = vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
    if (type == UINT32_MAX) type = vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0);
    if (type == UINT32_MAX) { r = VK_ERROR_FEATURE_NOT_PRESENT; goto fail; }
    if (!vk_pd_budget_allows(g_pd_bytes, req.size, pass_dump_budget())) { r = VK_ERROR_OUT_OF_DEVICE_MEMORY; goto fail; }
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, req.size, type};
    if ((r = vkAllocateMemory(V.dev, &ai, NULL, &g_input_memory)) != VK_SUCCESS ||
        (r = vkBindBufferMemory(V.dev, g_input_buffer, g_input_memory, 0)) != VK_SUCCESS ||
        (r = vkMapMemory(V.dev, g_input_memory, 0, VK_WHOLE_SIZE, 0, &g_input_map)) != VK_SUCCESS)
        goto fail;
    g_pd_bytes += req.size;
    return 1;
fail:
    input_capture_free();
    g_input_alloc_failed = 1;
    VKLOG("[RED] draw input capture: no readback buffer available (VkResult %d)", r);
    return 0;
}

static int input_capture_add(input_sample_t sample, const char *kind, uint64_t address,
                             VkBuffer buffer, VkDeviceSize offset, uint64_t size, int prefix) {
    if (g_input_count + g_input_pending >= INPUT_MAX || !address || !buffer) return 0;
    vkpool_t *pool = vk_pool_of(address, 1);
    if (!pool || pool->buf != buffer || offset != address - pool->lo + pool->boff) return 0;
    uint64_t available = pool->hi - address;
    if (prefix) {
        if (size > available) size = available;
        size &= ~UINT64_C(3);
    }
    if (!size || size > available || size > INPUT_BYTES) return 0;
    sample.kind = kind;
    sample.buffer = buffer;
    sample.offset = offset;
    sample.address = address;
    sample.pool = pool->base;
    sample.pool_serial = pool->serial;
    sample.pool_offset = address - pool->base;
    sample.size = (unsigned)size;
    g_inputs[g_input_count + g_input_pending++] = sample;
    return 1;
}

static void input_capture_end_pass(void) {
    if (!g_input_pending) return;
    if (g_pass_wmem || g_tf_in_pass) {
        g_input_rejected += g_input_pending;
        goto reset;
    }
    if (!input_capture_allocate()) goto reset;
    VkCommandBuffer cb = vk_cmd();
    VkMemoryBarrier before = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
        VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &before, 0, NULL, 0, NULL);
    for (unsigned i = g_input_count; i < g_input_count + g_input_pending; i++) {
        const input_sample_t *s = &g_inputs[i];
        VkBufferCopy copy = {s->offset, (VkDeviceSize)i * INPUT_BYTES, s->size};
        vkCmdCopyBuffer(cb, s->buffer, g_input_buffer, 1, &copy);
    }
    // Finish reading the source ranges before later Vulkan commands can
    // overwrite them, and make the copies visible to the host after its fence.
    VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &after, 0, NULL, 0, NULL);
    g_input_count += g_input_pending;
reset:
    g_input_pending = g_input_draws = 0;
}

static void input_capture_write(void) {
    if (!g_input_capture) return;
    if (g_input_count) {
        VkMappedMemoryRange inv = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL,
                                   g_input_memory, 0, VK_WHOLE_SIZE};
        if (vkInvalidateMappedMemoryRanges(V.dev, 1, &inv) == VK_SUCCESS) {
            char path[1024], tmp[1024];
            input_capture_path(path, "");
            input_capture_path(tmp, ".tmp");
            FILE *f = fopen(tmp, "w");
            if (f) {
                fputs("present\tsubmission\tpass\tdraw\tprior_dispatches\tvs\tfs\tgs\tkind\tindex_type\tstream\tstride\tcount\tguest_address\tpool\tpool_offset\tpool_serial\tvk_buffer_offset\tbinding_extent\tdivisor\tattributes\tbytes\tfnv1a64\traw_hex\n", f);
                for (unsigned i = 0; i < g_input_count; i++) {
                    const input_sample_t *s = &g_inputs[i];
                    const uint8_t *bytes = (const uint8_t *)g_input_map + i * INPUT_BYTES;
                    fprintf(f, "%llu\t%llu\t%d\t%llu\t%llu\t%016llx\t%016llx\t%016llx\t%s\t%d\t%d\t%u\t%u\t%llx\t%llx\t%llu\t%llu\t%llu\t%llu\t%u\t",
                        (unsigned long long)s->present, (unsigned long long)s->submission, s->pass,
                        (unsigned long long)s->draw, (unsigned long long)s->dispatches,
                        (unsigned long long)s->vs, (unsigned long long)s->fs, (unsigned long long)s->gs,
                        s->kind, s->index_type, s->stream, s->stride, s->count,
                        (unsigned long long)s->address, (unsigned long long)s->pool,
                        (unsigned long long)s->pool_offset, (unsigned long long)s->pool_serial,
                        (unsigned long long)s->offset, (unsigned long long)s->binding_extent, s->divisor);
                    for (unsigned a = 0; a < s->attr_count; a++)
                        fprintf(f, "%s%u:%u:%u", a ? "," : "", s->attr[a].location,
                                s->attr[a].format, s->attr[a].offset);
                    fprintf(f, "\t%u\t%016llx\t", s->size, (unsigned long long)vk_pd_hash(bytes, s->size));
                    for (unsigned j = 0; j < s->size; j++) fprintf(f, "%02x", bytes[j]);
                    fputc('\n', f);
                }
                int ok = !ferror(f);
                if (fclose(f) != 0) ok = 0;
                if (!ok || rename(tmp, path)) {
                    unlink(tmp);
                    VKLOG("[RED] draw input capture: could not write completed samples");
                }
            } else VKLOG("[RED] draw input capture: could not open report");
        } else VKLOG("[RED] draw input capture: host cache invalidation failed");
    }
    VKLOG("draw input capture: %u completed copies; %u samples excluded from passes with graphics writes",
          g_input_count, g_input_rejected);
    input_capture_free();
    g_input_capture = 0;
    g_input_count = 0;
}
