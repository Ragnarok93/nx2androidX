// Metadata-only replay observation. Resource handles are identities only and
// never retained by the presenter. The most recent plausible enlargement is
// reported as evidence, without authorizing replacement of the guest draw.
enum { SCENE_EVENTS = 8192, SCENE_HOPS = 16 };
enum scene_access { SCENE_READ, SCENE_AREA, SCENE_COLOR, SCENE_WRITE, SCENE_COPY_SRC, SCENE_COPY_DST };
typedef struct {
    surface_binding_t binding;
    enum scene_access access;
    int load, store;
} scene_event;

static scene_event g_scene_events[SCENE_EVENTS];
static unsigned g_scene_count;
static int g_scene_overflow;

static void scene_observe_reset(void) {
    g_scene_count = 0;
    g_scene_overflow = 0;
}

static int scene_exact(const surface_binding_t *b) {
    return b->image && b->write_sequence && b->aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
        b->mips == 1 && b->layers == 1 && b->samples == 1 && b->w && b->h && b->mip < 32;
}

static int scene_overlap(const surface_binding_t *a, const surface_binding_t *b) {
    if (a->image && a->image == b->image) {
        return (!a->mips || !b->mips ||
            ((uint64_t)a->mip + a->mips > b->mip && (uint64_t)b->mip + b->mips > a->mip)) &&
            (!a->layers || !b->layers ||
            ((uint64_t)a->layer + a->layers > b->layer && (uint64_t)b->layer + b->layers > a->layer));
    }
    if (!a->pool || a->pool != b->pool) return 0;
    if (a->offset < 0 || b->offset < 0 || a->size <= 0 || b->size <= 0) return 1;
    return (uint64_t)a->offset + a->size > (uint64_t)b->offset &&
        (uint64_t)b->offset + b->size > (uint64_t)a->offset;
}

static int scene_same_resource(const surface_binding_t *a, const surface_binding_t *b) {
    return scene_exact(a) && scene_exact(b) && a->image == b->image && a->pool == b->pool &&
        a->mip == b->mip && a->layer == b->layer && a->w == b->w && a->h == b->h &&
        a->storage_format == b->storage_format && a->view_format == b->view_format;
}

static int scene_same_pass(const surface_binding_t *a, const surface_binding_t *b) {
    return a->present == b->present && a->submission == b->submission && a->pass == b->pass;
}

static int scene_writer(const surface_binding_t *input, unsigned before, int load) {
    while (before) {
        const scene_event *event = &g_scene_events[--before];
        const surface_binding_t *b = &event->binding;
        if (b->present != input->present || event->access == SCENE_READ || event->access == SCENE_AREA ||
            event->access == SCENE_COPY_SRC || !scene_overlap(b, input)) continue;
        if (!scene_same_resource(b, input) ||
            (event->access != SCENE_COPY_DST && (event->access != SCENE_COLOR || event->store != 0)))
            break;
        if (load ? b->write_sequence < input->write_sequence && !scene_same_pass(b, input) :
                   b->write_sequence == input->write_sequence)
            return (int)before;
        break;
    }
    return -1;
}

static const surface_binding_t *scene_observe_source(const surface_binding_t *shown) {
    if (g_scene_overflow || !scene_exact(shown) || shown->present_crop[0] || shown->present_crop[1] ||
        shown->present_crop[2] != shown->w || shown->present_crop[3] != shown->h) return NULL;
    int producer = scene_writer(shown, g_scene_count, 0);
    for (unsigned hop = 0; producer >= 0 && hop < SCENE_HOPS; ++hop) {
        const scene_event *event = &g_scene_events[producer];
        const surface_binding_t *output = &event->binding;
        if (event->access == SCENE_COPY_DST) {
            if (!producer || g_scene_events[producer - 1].access != SCENE_COPY_SRC) return NULL;
            const surface_binding_t *source = &g_scene_events[producer - 1].binding;
            if (!scene_same_pass(source, output) || scene_overlap(source, output)) return NULL;
            producer = scene_writer(source, (unsigned)producer - 1, 0);
            continue;
        }
        int sampled = -1;
        for (int i = producer - 1; i >= 0; --i) {
            const scene_event *input = &g_scene_events[i];
            if (!scene_same_pass(&input->binding, output)) break;
            if (input->access != SCENE_READ) continue;
            if (sampled >= 0 || input->binding.stage != 1) { sampled = -1; break; }
            sampled = i;
        }
        if (sampled >= 0) {
            const surface_binding_t *source = &g_scene_events[sampled].binding;
            int previous = scene_writer(source, (unsigned)sampled, 0);
            int stable = previous >= 0 && !scene_same_pass(&g_scene_events[previous].binding, output) &&
                scene_exact(source) && source->view && source->view_format && !scene_overlap(source, output) &&
                output->draws > source->draws && output->draws - source->draws == 1;
            unsigned full_areas = 0;
            for (int i = producer - 1; stable && i >= 0 &&
                 scene_same_pass(&g_scene_events[i].binding, output); --i) {
                const scene_event *access = &g_scene_events[i];
                if (access->access == SCENE_AREA) {
                    const surface_binding_t *area = &access->binding;
                    full_areas += !area->render_x && !area->render_y &&
                        area->w == (output->w >> output->mip) && area->h == (output->h >> output->mip);
                } else if (access->access != SCENE_READ) stable = 0;
            }
            if (stable && full_areas == 1 && source->view_format == output->view_format) {
                uint64_t sw = source->w >> source->mip, sh = source->h >> source->mip;
                uint64_t dw = output->w >> output->mip, dh = output->h >> output->mip;
                uint64_t lhs = sw * dh, rhs = sh * dw;
                if (sw && sh && sw <= dw && sh <= dh && (sw < dw || sh < dh) &&
                    (lhs > rhs ? lhs - rhs : rhs - lhs) <= 2 * (dw > dh ? dw : dh))
                    return source;
            }
        }
        if (event->load != 0) return NULL;
        producer = scene_writer(output, (unsigned)producer, 1);
    }
    return NULL;
}

static void scene_observe_access(const char *role, const surface_binding_t *b,
                                 int load, int store, const char *status) {
    enum scene_access access;
    if (!strcmp(role, "sampled-binding")) access = SCENE_READ;
    else if (!strcmp(role, "render-area")) access = SCENE_AREA;
    else if (!strcmp(role, "color")) access = SCENE_COLOR;
    else if (!strcmp(role, "copy-source") && !strcmp(status, "recorded-full-color-transfer"))
        access = SCENE_COPY_SRC;
    else if (!strcmp(role, "copy-destination") && !strcmp(status, "recorded-full-color-transfer"))
        access = SCENE_COPY_DST;
    else if (!strcmp(role, "storage-binding") || !strcmp(role, "compute-image") ||
             !strcmp(role, "depth") || !strcmp(role, "stencil") ||
             !strcmp(role, "copy-destination") || !strcmp(role, "buffer-copy-destination") ||
             !strcmp(role, "texture-clear") || !strcmp(role, "texture-clear-output"))
        access = SCENE_WRITE;
    else return;

    if (g_scene_count == SCENE_EVENTS) {
        g_scene_overflow = 1;
        return;
    }
    g_scene_events[g_scene_count++] = (scene_event){*b, access, load, store};
}
