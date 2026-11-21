// The replay thread owns the Vulkan readbacks. The archive worker freezes
// publication while it reads the files and leaves the session armed on failure.
enum { CAPTURE_SLOTS = 3, CAPTURE_INTERVAL = 30 };
static pthread_mutex_t g_capture_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_capture_exporting;
static int g_capture_delay;
static unsigned g_capture_next_slot;
static uint64_t g_capture_next_present;
static int g_capture_rolling_dump;
static char g_capture_slot[896];
static FILE *g_capture_timeline;

static const char *vk_pass_dump_root(void) {
    if (g_capture_rolling_dump) return g_capture_slot;
    const char *root = getenv("DM_ROOT");
    return root ? root : ".";
}

int vk_pass_dump_user_begin(int delay) {
    pthread_mutex_lock(&g_capture_lock);
    if (g_capture_exporting || g_capture_rolling_dump || atomic_load(&g_pd_user_pending)) {
        pthread_mutex_unlock(&g_capture_lock);
        return 0;
    }
    const char *root = vk_pass_dump_root();
    char path[1024];
    for (unsigned i = 0; i < CAPTURE_SLOTS; i++) {
        snprintf(path, sizeof path, "%s/diagnostic-capture-%u/complete", root, i);
        unlink(path);
    }
    snprintf(path, sizeof path, "%s/capture_timeline.tsv", root);
    g_capture_timeline = fopen(path, "w");
    if (g_capture_timeline)
        fputs("present\tsubmission\tdraws_total\tpasses_total\tdispatches_total\tgeometry\n", g_capture_timeline);
    g_capture_delay = delay + 1; // let the pause overlay leave the screen
    g_capture_next_slot = 0;
    g_capture_next_present = 0;
    g_pd_scene_waited = 0;
    atomic_store(&g_pd_user_pending, 1);
    pthread_mutex_unlock(&g_capture_lock);
    return 1;
}

int vk_pass_dump_export_begin(void) {
    pthread_mutex_lock(&g_capture_lock);
    int available = !g_capture_exporting;
    if (available) {
        g_capture_exporting = 1;
        if (g_capture_timeline) fflush(g_capture_timeline);
    }
    pthread_mutex_unlock(&g_capture_lock);
    return available;
}

void vk_pass_dump_export_end(int saved) {
    pthread_mutex_lock(&g_capture_lock);
    if (g_capture_exporting && saved) {
        atomic_store(&g_pd_user_pending, 0);
        if (g_capture_timeline) fclose(g_capture_timeline);
        g_capture_timeline = NULL;
    }
    g_capture_exporting = 0;
    pthread_mutex_unlock(&g_capture_lock);
}

int vk_pass_dump_user_active(void) { return atomic_load(&g_pd_user_pending); }

static int capture_start_slot(void) {
    int n = snprintf(g_capture_slot, sizeof g_capture_slot,
                     "%s/diagnostic-capture-%u", vk_pass_dump_root(), g_capture_next_slot);
    if (n < 0 || (size_t)n >= sizeof g_capture_slot) return 0;
    if (mkdir(g_capture_slot, 0700) != 0 && errno != EEXIST) return 0;
    struct stat st;
    if (lstat(g_capture_slot, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    char path[1024];
    static const char *const reports[] = {
        "complete", "pass_dump_manifest.tsv", "gpu_draw_inputs.tsv", "surface_usage.tsv",
        "surface_samples.tsv", "surface_samples.raw",
    };
    for (unsigned i = 0; i < sizeof reports / sizeof reports[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", g_capture_slot, reports[i]);
        unlink(path);
    }
    g_capture_next_slot = (g_capture_next_slot + 1) % CAPTURE_SLOTS;
    g_capture_rolling_dump = 1;
    return 1;
}

static void capture_finish_slot(void) {
    if (atomic_load(&g_pd_user_pending)) {
        char path[1024];
        snprintf(path, sizeof path, "%s/complete", g_capture_slot);
        FILE *file = fopen(path, "w");
        if (file) fclose(file);
    }
    g_capture_rolling_dump = 0;
}
