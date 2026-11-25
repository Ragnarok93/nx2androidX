package org.nx2a;

import android.app.Activity;
import android.app.Dialog;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.view.Window;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.AtomicFile;
import android.util.Log;
import android.widget.Toast;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import org.json.JSONObject;

/** Per-game driver import. The selected library is outside the replaceable game pack. */
final class DriverDocuments {
    private static final int REQUEST = 0x6e34;
    private static final long LIMIT = 512L * 1024 * 1024;
    private final GameActivity activity;
    private boolean busy;
    DriverDocuments(GameActivity activity) { this.activity = activity; }
    private AtomicFile marker() {
        return new AtomicFile(new File(activity.getFilesDir(), "driver-selection"));
    }
    // Keep the native marker ABI unchanged: only the next process loads the selection.
    private static final int SURFACE = Color.rgb(39, 37, 43);
    private static final int TEXT = Color.rgb(230, 225, 229);
    private static final int MUTED = Color.rgb(202, 196, 208);
    private static final int BLUE = Color.rgb(3, 129, 254);
    private static final int LINK = Color.rgb(62, 145, 255);
    private static final String ENTRY = ".driver-entry.json";

    private static final class DriverEntry {
        final String path, label;
        DriverEntry(String path, String label) { this.path = path; this.label = label; }
    }

    private String selection() {
        try {
            byte[] b = marker().readFully();
            if (b.length > 383) return "";
            String value = new String(b, StandardCharsets.US_ASCII).trim();
            return "system".equals(value) || value.matches("drivers/[0-9a-f]{32}/[A-Za-z0-9][A-Za-z0-9._-]{0,119}\\.so") ? value : "";
        } catch (IOException e) { return ""; }
    }

    private static JSONObject readJson(File file) throws Exception {
        if (!file.isFile() || file.length() > 65536) throw new IOException("Invalid driver metadata");
        byte[] bytes = new byte[(int) file.length()];
        try (DataInputStream in = new DataInputStream(new FileInputStream(file))) { in.readFully(bytes); }
        return new JSONObject(new String(bytes, StandardCharsets.UTF_8));
    }

    private static void writeEntry(File stage, String library, String display) throws Exception {
        JSONObject entry = new JSONObject();
        entry.put("library", library);
        entry.put("label", display == null || display.trim().isEmpty() ? library : display.substring(0, Math.min(180, display.length())));
        try (FileOutputStream out = new FileOutputStream(new File(stage, ENTRY))) {
            out.write(entry.toString().getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
    }

    // Older builds already retained UUID directories. Recover their exact entry
    // point from the selection marker, ZIP metadata, or the old import heuristic.
    private List<DriverEntry> installed(String current) {
        ArrayList<DriverEntry> entries = new ArrayList<>();
        File[] dirs = new File(activity.getFilesDir(), "drivers").listFiles();
        if (dirs == null) return entries;
        for (File dir : dirs) {
            String id = dir.getName();
            if (!dir.isDirectory() || !id.matches("[0-9a-f]{32}")) continue;
            String prefix = "drivers/" + id + "/";
            try {
                String library = null, label = null;
                File manifest = new File(dir, ENTRY);
                if (manifest.isFile()) {
                    JSONObject data = readJson(manifest);
                    library = data.optString("library");
                    label = data.optString("label");
                } else if (current.startsWith(prefix)) {
                    library = current.substring(prefix.length());
                } else if (new File(dir, "meta.json").isFile()) {
                    JSONObject meta = readJson(new File(dir, "meta.json"));
                    library = meta.optString("libraryName");
                } else {
                    File[] libs = dir.listFiles(f -> f.isFile() && safeLibrary(f.getName()));
                    if (libs == null) continue;
                    for (File lib : libs) {
                        String name = lib.getName().toLowerCase(Locale.ROOT);
                        if (name.contains("vulkan") || name.contains("radv")) {
                            if (library != null) { library = null; break; }
                            library = lib.getName();
                        }
                    }
                    if (library == null && libs.length == 1) library = libs[0].getName();
                }
                if (!safeLibrary(library)) continue;
                File file = new File(dir, library);
                if (!file.isFile() || !file.canRead() || file.length() < 64) continue;
                if (label == null || label.trim().isEmpty()) {
                    label = library;
                    try {
                        JSONObject meta = readJson(new File(dir, "meta.json"));
                        String name = meta.optString("name"), version = meta.optString("version");
                        if (!name.isEmpty()) label = name + (version.isEmpty() ? "" : " " + version);
                    } catch (Exception ignored) { }
                }
                // Short directory ID disambiguates packages with the same name.
                entries.add(new DriverEntry(prefix + library, label + " · " + id.substring(0, 8)));
            } catch (Exception e) { Log.w("nx2a-driver", "Skipping invalid driver entry " + id, e); }
        }
        Collections.sort(entries, (a, b) -> a.label.compareToIgnoreCase(b.label));
        return entries;
    }

    private int dp(float n) { return Math.round(n * activity.getResources().getDisplayMetrics().density); }

    private TextView text(String value, int size, int color) {
        TextView view = new TextView(activity);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        return view;
    }

    private Button action(String value, Runnable callback) {
        Button button = new Button(activity);
        button.setText(value);
        button.setAllCaps(false);
        button.setTextColor(LINK);
        button.setBackgroundColor(Color.TRANSPARENT);
        button.setOnClickListener(v -> callback.run());
        return button;
    }

    void choose() {
        if (busy || activity.isFinishing() || activity.isDestroyed()) return;
        String current = selection();
        List<DriverEntry> entries = installed(current);
        entries.add(0, new DriverEntry("system", "System Vulkan"));
        entries.add(1, new DriverEntry("", "Original APK default"));
        String next = current.isEmpty() ? "Original APK default" : current.equals("system") ? "System Vulkan" : "Unavailable driver (system fallback)";
        for (DriverEntry entry : entries) if (entry.path.equals(current)) next = entry.label;

        // Explicit opaque content rather than recoloring a vendor AlertDialog window:
        // One UI may install a separate light background on its AlertDialog decor.
        Dialog dialog = new Dialog(activity, android.R.style.Theme_Material_Dialog_NoActionBar);
        LinearLayout panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(22), dp(18), dp(22), dp(10));
        GradientDrawable surface = new GradientDrawable();
        surface.setColor(SURFACE);
        surface.setCornerRadius(dp(28));
        panel.setBackground(surface);
        TextView title = text("Vulkan driver", 21, TEXT);
        title.setTypeface(null, Typeface.BOLD);
        panel.addView(title);
        TextView status = text("Active: " + GameActivity.activeVulkanDriver() + "\nNext launch: " + next, 14, TEXT);
        status.setPadding(0, dp(8), 0, dp(8));
        panel.addView(status);

        RadioGroup options = new RadioGroup(activity);
        options.setOrientation(LinearLayout.VERTICAL);
        int checked = View.NO_ID;
        ColorStateList tint = new ColorStateList(new int[][]{
            new int[]{android.R.attr.state_checked}, new int[]{}}, new int[]{BLUE, MUTED});
        for (DriverEntry entry : entries) {
            RadioButton row = new RadioButton(activity);
            row.setId(View.generateViewId());
            row.setText(entry.label);
            row.setTextColor(TEXT);
            row.setButtonTintList(tint);
            row.setTextSize(15);
            row.setMinHeight(dp(44));
            options.addView(row);
            if (entry.path.equals(current)) checked = row.getId();
            row.setOnClickListener(v -> {
                try {
                    if (entry.path.isEmpty()) {
                        marker().delete();
                        if (marker().getBaseFile().exists()) throw new IOException("Cannot restore APK default");
                    } else {
                        if (!entry.path.equals("system") && !new File(activity.getFilesDir(), entry.path).isFile())
                            throw new IOException("Driver no longer exists");
                        commit(entry.path);
                    }
                    dialog.dismiss();
                    toast(entry.label + " selected. Fully close and reopen the game.");
                } catch (IOException e) {
                    dialog.dismiss();
                    toast("Cannot save driver selection: " + e.getMessage());
                    choose();
                }
            });
        }
        if (checked != View.NO_ID) options.check(checked);
        ScrollView scroll = new ScrollView(activity);
        scroll.addView(options);
        // A bounded list leaves the actions reachable in landscape, even with many imports.
        int screenHeight = activity.getResources().getDisplayMetrics().heightPixels;
        int listHeight = Math.min(dp(44 * Math.min(entries.size(), 6)), Math.max(dp(44), screenHeight / 3));
        panel.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, listHeight));
        TextView hint = text("Changes apply after fully closing and reopening the game. Import only trusted drivers for your GPU.", 12, MUTED);
        hint.setPadding(0, dp(8), 0, dp(4));
        panel.addView(hint);
        LinearLayout actions = new LinearLayout(activity);
        actions.setGravity(android.view.Gravity.END);
        actions.addView(action("Cancel", dialog::dismiss));
        actions.addView(action("Import ZIP / SO", () -> { dialog.dismiss(); pick(); }));
        panel.addView(actions);
        dialog.setContentView(panel);
        dialog.show();
        Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.addFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            window.setDimAmount(0.5f);
            window.setLayout(Math.min(dp(540), activity.getResources().getDisplayMetrics().widthPixels - dp(24)),
                    ViewGroup.LayoutParams.WRAP_CONTENT);
        }
    }
    private void pick() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*"); // providers often report driver ZIPs as application/octet-stream
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { activity.startActivityForResult(i, REQUEST); }
        catch (Exception e) { toast("Cannot open driver picker: " + e.getMessage()); }
    }
    boolean onResult(int request, int result, Intent data) {
        if (request != REQUEST) return false;
        if (result != Activity.RESULT_OK || data == null || data.getData() == null || busy) return true;
        busy = true;
        Uri uri = data.getData();
        new Thread(() -> {
            File stage = null;
            boolean committed = false;
            try {
                File root = new File(activity.getFilesDir(), "drivers");
                if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create driver directory");
                String id = UUID.randomUUID().toString().replace("-", "");
                stage = new File(root, ".partial-" + id);
                if (!stage.mkdir()) throw new IOException("Cannot stage driver");
                String display = null;
                try (Cursor c = activity.getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                    if (c != null && c.moveToFirst()) display = c.getString(0);
                }
                String library;
                try (InputStream in = activity.getContentResolver().openInputStream(uri)) {
                    if (in == null) throw new IOException("Cannot read selected document");
                    library = unpack(in, display, stage);
                }
                writeEntry(stage, library, display);
                File target = new File(root, id);
                if (!stage.renameTo(target)) throw new IOException("Cannot publish driver");
                committed = true; // keep the saved import even if selecting it fails
                commit("drivers/" + id + "/" + library);
                final String message = library + " saved and selected. Fully close and reopen the game to activate it.";
                activity.runOnUiThread(() -> toast(message));
            } catch (Exception e) {
                Log.e("nx2a-driver", "Driver import failed", e);
                activity.runOnUiThread(() -> toast("Driver import failed: " + e.getMessage()));
            } finally {
                if (!committed && stage != null) GameActivity.deleteTree(stage);
                activity.runOnUiThread(() -> busy = false);
            }
        }, "nx2a-driver-import").start();
        return true;
    }
    private void toast(String text) {
        if (!activity.isDestroyed()) Toast.makeText(activity, text, Toast.LENGTH_LONG).show();
    }
    private void commit(String value) throws IOException {
        AtomicFile marker = marker();
        FileOutputStream out = marker.startWrite();
        try {
            out.write((value + "\n").getBytes(StandardCharsets.US_ASCII));
            out.getFD().sync();
            marker.finishWrite(out);
        } catch (IOException e) {
            marker.failWrite(out);
            throw e;
        }
    }
    private static boolean safeLibrary(String s) {
        return s != null && s.matches("[A-Za-z0-9][A-Za-z0-9._-]{0,119}\\.so") &&
            !s.equals("libdmhost.so") && !s.equals("libadrenotools.so") && !s.startsWith("libhook_") &&
            !s.equals("libmain_hook.so") && !s.equals("libfile_redirect_hook.so") && !s.equals("libgsl_alloc_hook.so");
    }
    private static String unpack(InputStream in, String display, File stage) throws Exception {
        BufferedInputStream src = new BufferedInputStream(in);
        src.mark(4);
        int first = src.read(), second = src.read();
        src.reset();
        if (first == 0x7f && second == 'E') {
            if (!safeLibrary(display)) throw new IOException("Select an ARM64 Vulkan .so library or driver ZIP");
            copy(src, new File(stage, display), LIMIT);
            validateElf(new File(stage, display));
            return display;
        }
        if (first != 'P' || second != 'K') throw new IOException("Select a Vulkan driver ZIP or ARM64 .so");
        long remaining = LIMIT;
        int count = 0;
        Set<String> libraries = new HashSet<>();
        String meta = null;
        try (ZipInputStream zip = new ZipInputStream(src)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (++count > 128) throw new IOException("Too many driver archive entries");
                String path = entry.getName();
                if (path.startsWith("/") || path.contains("\\") || path.contains(":") ||
                    Arrays.asList(path.split("/")).contains("..")) throw new IOException("Unsafe driver archive path");
                String name = path.substring(path.lastIndexOf('/') + 1);
                if (!entry.isDirectory() && "meta.json".equals(name)) {
                    if (meta != null) throw new IOException("Duplicate driver metadata");
                    File f = new File(stage, "meta.json");
                    remaining -= copy(zip, f, Math.min(remaining, 65536));
                    try (InputStream m = new FileInputStream(f)) {
                        byte[] bytes = new byte[(int) f.length()];
                        new DataInputStream(m).readFully(bytes);
                        meta = new String(bytes, StandardCharsets.UTF_8);
                    }
                } else if (!entry.isDirectory() && safeLibrary(name)) {
                    if (!libraries.add(name)) throw new IOException("Duplicate driver library: " + name);
                    File f = new File(stage, name);
                    remaining -= copy(zip, f, remaining);
                    validateElf(f);
                } else {
                    // Bound even unneeded files: ZipInputStream.closeEntry otherwise inflates them without a limit.
                    byte[] buffer = new byte[32768];
                    int n;
                    while ((n = zip.read(buffer)) != -1) {
                        remaining -= n;
                        if (remaining < 0) throw new IOException("Driver archive exceeds 512 MiB");
                    }
                }
                zip.closeEntry();
            }
        }
        String chosen = null;
        if (meta != null) {
            chosen = new JSONObject(meta).optString("libraryName", "");
            if (!safeLibrary(chosen) || !libraries.contains(chosen))
                throw new IOException("Driver metadata does not name an included library");
        } else {
            for (String name : libraries) {
                if (name.toLowerCase(Locale.ROOT).contains("vulkan") || name.toLowerCase(Locale.ROOT).contains("radv")) {
                    if (chosen != null) throw new IOException("Multiple Vulkan libraries; meta.json is required");
                    chosen = name;
                }
            }
            if (chosen == null && libraries.size() == 1) chosen = libraries.iterator().next();
        }
        if (chosen == null) throw new IOException("No Vulkan driver library found");
        return chosen;
    }
    private static long copy(InputStream in, File file, long max) throws IOException {
        long total = 0;
        try (FileOutputStream out = new FileOutputStream(file)) {
            byte[] buffer = new byte[65536];
            int n;
            while ((n = in.read(buffer)) != -1) {
                total += n;
                if (total > max) throw new IOException("Driver archive exceeds size limit");
                out.write(buffer, 0, n);
            }
            out.getFD().sync();
        }
        return total;
    }
    private static void validateElf(File file) throws IOException {
        byte[] h = new byte[64];
        try (DataInputStream in = new DataInputStream(new FileInputStream(file))) { in.readFully(h); }
        if (h[0] != 0x7f || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' ||
            h[4] != 2 || h[5] != 1 || h[16] != 3 || h[17] != 0 || (h[18] & 255) != 183 || h[19] != 0)
            throw new IOException("Not an ARM64 shared library: " + file.getName());
        // The loader needs readable private executable mappings, not a writable library.
        if (!file.setReadOnly()) throw new IOException("Cannot protect driver library");
    }
}
