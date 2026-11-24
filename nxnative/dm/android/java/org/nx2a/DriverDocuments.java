package org.nx2a;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
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
    private String selected() {
        try {
            byte[] b = marker().readFully();
            if (b.length > 384) return "Invalid";
            String text = new String(b, StandardCharsets.US_ASCII).trim();
            if ("system".equals(text)) return "System Vulkan";
            return new File(text).getName();
        } catch (IOException e) { return "Original APK default"; }
    }
    void choose() {
        if (busy || activity.isFinishing()) return;
        new AlertDialog.Builder(activity)
            .setTitle("Vulkan driver")
            .setMessage("Active: " + GameActivity.activeVulkanDriver() +
                        "\nNext launch: " + selected() +
                        "\n\nChanges apply after fully closing and reopening the game. Import only trusted drivers for your GPU.")
            .setPositiveButton("Import ZIP / SO", (d, w) -> pick())
            .setNeutralButton("System Vulkan", (d, w) -> selectSystem())
            .setNegativeButton("Cancel", null)
            .show();
    }
    private void pick() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*"); // providers often report driver ZIPs as application/octet-stream
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { activity.startActivityForResult(i, REQUEST); }
        catch (Exception e) { toast("Cannot open driver picker: " + e.getMessage()); }
    }
    private void selectSystem() {
        try {
            commit("system");
            toast("System Vulkan selected. Fully close and reopen the game.");
        } catch (IOException e) { toast("Cannot save driver selection: " + e.getMessage()); }
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
                stage = new File(root, UUID.randomUUID().toString().replace("-", ""));
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
                commit("drivers/" + stage.getName() + "/" + library);
                committed = true;
                final String message = library + " selected. Fully close and reopen the game to activate it.";
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
