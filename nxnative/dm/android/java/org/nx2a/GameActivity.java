package org.nx2a;

import android.app.NativeActivity;
import android.app.Activity;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.database.Cursor;
import android.os.Bundle;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.os.StatFs;
import android.util.Log;
import android.widget.Toast;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.nio.charset.StandardCharsets;

/** Standalone game Activity: unpack the embedded port before the NativeActivity host starts. */
public class GameActivity extends NativeActivity {
    // NativeActivity's loadNativeCode() opens libdmhost.so for
    // ANativeActivity_onCreate, but does NOT register it as a JNI library
    // with ART. Our embeddedPackReady()/companionPackReady() callbacks
    // require System.loadLibrary for Java's native-method resolution.
    // The native loader then reuses the same library and class loader.
    static {
        System.loadLibrary("dmhost");
    }

    private static final int REQUEST_LOSSLESS_DLL = 0x6e33;
    private final DriverDocuments driverDocuments = new DriverDocuments(this);
    static native String activeVulkanDriver();

    /** Graphics -> Vulkan driver. The current VkDevice is never modified. */
    public void chooseVulkanDriver() {
        runOnUiThread(driverDocuments::choose);
    }
    private String diagnosticsRoot;
    static native boolean diagnosticsExportStarted();
    static native void diagnosticsExported(int result); // 0 failed, 1 saved, 2 cancelled

    /** Called by the native pause-menu Diagnostics tab, never from gameplay. */
    public void exportDiagnostics(String root) {
        diagnosticsRoot = root;
        DiagnosticsExport.request(this);
    }

    /** Called only by the Java archive worker, after the Android document picker. */
    static void notifyDiagnosticsExport(int result) {
        try { diagnosticsExported(result); }
        catch (UnsatisfiedLinkError error) { Log.e("nx2a-diag", "Native export status unavailable", error); }
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        if (diagnosticsRoot != null) state.putString("nx2a.diagnosticsRoot", diagnosticsRoot);
        super.onSaveInstanceState(state);
    }
    static native String saveArchiveRoot();
    static native String saveExportSnapshot();
    static native void saveExportStatus(boolean success, String message);
    static native boolean saveImportStage(String name);
    /** System documents picker, allowing the player to place a ZIP anywhere. */
    public void chooseSaveExport() { SaveDocuments.chooseExport(this); }
    /** System documents picker, allowing the player to import any supported ZIP URI. */
    public void chooseSaveImport() { SaveDocuments.chooseImport(this); }
    private final GameKeyboard keyboard = new GameKeyboard(this);

    /** Called by the standalone performance menu when shader DLL is missing. */
    public void chooseLosslessDll() {
        runOnUiThread(() -> {
            Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            picker.addCategory(Intent.CATEGORY_OPENABLE);
            picker.setType("*/*");
            picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            try { startActivityForResult(picker, REQUEST_LOSSLESS_DLL); }
            catch (Exception error) {
                Log.e("nx2a-fg", "Unable to open Lossless.dll picker", error);
                Toast.makeText(this, "Cannot open file picker: " + error.getMessage(), Toast.LENGTH_LONG).show();
            }
        });
    }

    private void importLosslessDll(Uri selected) {
        new Thread(() -> {
            File partial = null;
            try {
                try (Cursor cursor = getContentResolver().query(selected,
                        new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                    if (cursor == null || !cursor.moveToFirst() ||
                            !"Lossless.dll".equalsIgnoreCase(cursor.getString(0)))
                        throw new IOException("Please select a file named Lossless.dll");
                }
                File directory = new File(getFilesDir(), "nx2a-data");
                if (!directory.isDirectory() && !directory.mkdirs())
                    throw new IOException("Cannot create game-data folder");
                File target = new File(directory, "Lossless.dll");
                partial = new File(directory, "Lossless.dll.partial");
                long total = 0;
                try (InputStream input = new BufferedInputStream(getContentResolver().openInputStream(selected));
                     FileOutputStream output = new FileOutputStream(partial)) {
                    if (input == null) throw new IOException("Cannot read selected file");
                    byte[] bytes = new byte[64 * 1024];
                    int n;
                    while ((n = input.read(bytes)) != -1) {
                        if (n <= 0) continue;
                        total += n;
                        if (total > (128L << 20)) throw new IOException("Lossless.dll exceeds 128 MiB");
                        output.write(bytes, 0, n);
                    }
                    output.getFD().sync();
                }
                if (total == 0) throw new IOException("Lossless.dll is empty");
                if (target.exists() && !target.delete())
                    throw new IOException("Cannot replace previous Lossless.dll");
                if (!partial.renameTo(target)) throw new IOException("Cannot activate Lossless.dll");
                runOnUiThread(() -> {
                    losslessDllReady(target.getAbsolutePath());
                    Toast.makeText(this, "Lossless FG shaders imported", Toast.LENGTH_SHORT).show();
                });
            } catch (Exception error) {
                Log.e("nx2a-fg", "Lossless.dll import failed", error);
                runOnUiThread(() -> Toast.makeText(this, "Lossless.dll import failed: " +
                        error.getMessage(), Toast.LENGTH_LONG).show());
            } finally {
                if (partial != null) partial.delete();
            }
        }, "nx2a-lossless-import").start();
    }

    private static String text(byte[] value) {
        return value == null ? "" : new String(value, StandardCharsets.UTF_8);
    }

    // Guest callers wait off the UI thread. Ordinary UTF-8, not JNI modified UTF-8.
    public byte[] readKeyboard(byte[] title, byte[] subtitle, byte[] guide, byte[] initial, int min, int max) {
        return keyboard.read(text(title), text(subtitle), text(guide), text(initial), min, max);
    }

    @Override protected void onCreate(Bundle state) {
        // NativeActivity loads libdmhost here. Its native data setup defers game startup for embedded APKs;
        // extraction then runs off the UI thread so multi-gigabyte packs cannot ANR the launcher.
        // Install the smooth system typeface before the native host can draw a menu.
        MenuTypography.install();
        super.onCreate(state);
        if (state != null) diagnosticsRoot = state.getString("nx2a.diagnosticsRoot");
        enterImmersiveMode();
        prepareEmbeddedPack(this);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (driverDocuments.onResult(requestCode, resultCode, data)) return;
        if (requestCode == DiagnosticsExport.REQUEST) {
            DiagnosticsExport.onResult(this, diagnosticsRoot, resultCode, data);
            return;
        }
        if (SaveDocuments.onActivityResult(this, requestCode, resultCode, data)) return;
        if (requestCode == REQUEST_LOSSLESS_DLL) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null)
                importLosslessDll(data.getData());
            return;
        }
        CompanionImporter.onActivityResult(this, requestCode, resultCode, data);
    }

    @Override protected void onResume() {
        super.onResume();
        keyboard.pause(false);
        enterImmersiveMode();
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) enterImmersiveMode();
    }

    // The system may reveal its bars with an edge swipe, but they are transient.
    // Re-enter after the document picker, keyboard dialog or a lifecycle transition.
    @SuppressWarnings("deprecation")
    private void enterImmersiveMode() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.systemBars());
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    static boolean hasEmbeddedPack(Context context) {
        try (InputStream marker = context.getAssets().open("nx2a-export-mode")) {
            return marker.read() >= 0;
        } catch (java.io.FileNotFoundException missing) {
            return false;
        } catch (IOException error) {
            Log.e("nx2a-export", "Could not read the embedded-data marker", error);
            return false;
        }
    }

    static void prepareEmbeddedPack(Activity activity) {
        if (CompanionImporter.prepare(activity)) return; // large APK + data: SAF first-run importer
        if (!hasEmbeddedPack(activity)) return;  // old desktop APKs retain the native folder picker
        ProgressDialog dialog = new ProgressDialog(activity);
        dialog.setMessage("Preparing game data…");
        dialog.setIndeterminate(true);
        dialog.setCancelable(false);
        dialog.show();
        Thread worker = new Thread(() -> {
            try {
                extractEmbeddedPack(activity);
                activity.runOnUiThread(() -> {
                    if (activity.isFinishing() || activity.isDestroyed()) return;
                    dialog.dismiss();
                    embeddedPackReady();
                });
            } catch (Exception error) {
                Log.e("nx2a-export", "Could not prepare the bundled game data", error);
                activity.runOnUiThread(() -> {
                    if (activity.isFinishing() || activity.isDestroyed()) return;
                    dialog.dismiss();
                    Toast.makeText(activity, "Game data extraction failed: " +
                            (error.getMessage() == null ? error.toString() : error.getMessage()),
                            Toast.LENGTH_LONG).show();
                    activity.finish();
                });
            }
        }, "nx2a-pack-extract");
        worker.start();
    }

    static native void embeddedPackReady();
    static native boolean companionPackReady(String absoluteDirectory);
    static native void losslessDllReady(String path);

    @Override protected void onPause() { keyboard.pause(true); super.onPause(); }
    @Override protected void onDestroy() { keyboard.pause(true); super.onDestroy(); }

    static void extractEmbeddedPack(Context activity) throws Exception {
        // Legacy split-folder APKs do not carry this marker; keep their NativeActivity data-folder flow intact.
        try (InputStream marker = activity.getAssets().open("nx2a-export-mode")) {
            if (marker.read() < 0) throw new IOException("empty export marker");
        } catch (java.io.FileNotFoundException missing) { return; }
        String expected;
        try (InputStream in = activity.getAssets().open("nx2a-data.sha256")) {
            byte[] b = new byte[256];
            int n = in.read(b);
            expected = new String(b, 0, Math.max(0, n), StandardCharsets.US_ASCII).trim().toLowerCase(Locale.ROOT);
        }
        if (!expected.matches("[0-9a-f]{64}")) throw new IOException("invalid embedded data checksum");
        File base = activity.getFilesDir();
        File target = new File(base, "nx2a-data");
        File partial = new File(base, "nx2a-data.partial");
        File backup = new File(base, "nx2a-data.backup");
        File marker = new File(target, "nx2a-export.sha256");
        if (target.isDirectory() && marker.isFile() && expected.equals(readText(marker).trim())) {
            preserveVulkanCache(base, backup);
            deleteTree(backup);  // a prior process may have died after activating the new directory
            return;
        }

        // Recover a previous directory if the process died between the two rename operations.
        if (!target.exists() && backup.isDirectory()) backup.renameTo(target);
        deleteTree(partial);
        if (!partial.mkdirs() && !partial.isDirectory()) throw new IOException("cannot create private data directory");

        String actual = digestAsset(activity, "nx2a-data.zip");
        if (!expected.equals(actual)) throw new IOException("bundled game data checksum mismatch");
        long available = new StatFs(base.getAbsolutePath()).getAvailableBytes();
        long written = 0;
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(activity.getAssets().open("nx2a-data.zip")))) {
            byte[] buffer = new byte[64 * 1024];
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                String name = entry.getName();
                if (!safeEntry(name)) throw new IOException("unsafe path in game data archive");
                File output = new File(partial, name);
                String root = partial.getCanonicalPath() + File.separator;
                if (!entry.isDirectory() && !output.getCanonicalPath().startsWith(root))
                    throw new IOException("game data path escapes its private directory");
                if (entry.isDirectory()) {
                    if (!output.isDirectory() && !output.mkdirs()) throw new IOException("cannot create game data folder");
                } else {
                    File parent = output.getParentFile();
                    if (parent != null && !parent.isDirectory() && !parent.mkdirs())
                        throw new IOException("cannot create game data folder");
                    try (BufferedOutputStream out = new BufferedOutputStream(new FileOutputStream(output))) {
                        int n;
                        while ((n = zip.read(buffer)) != -1) {
                            if (n == 0) continue;
                            if (n > available - written) throw new IOException("not enough free storage for game data");
                            out.write(buffer, 0, n);
                            written += n;
                        }
                    }
                }
                zip.closeEntry();
            }
        } catch (Exception e) {
            deleteTree(partial);
            throw e;
        }
        if (!new File(partial, "nx2a-data.json").isFile()) {
            deleteTree(partial);
            throw new IOException("game data archive is incomplete");
        }
        // Keep the player's in-game settings across an APK update/re-export.
        File oldSettings = new File(target, "settings.txt");
        if (oldSettings.isFile()) copyFile(oldSettings, new File(partial, "settings.txt"));
        writeText(new File(partial, "nx2a-export.sha256"), expected + "\n");

        deleteTree(backup);
        if (target.exists() && !target.renameTo(backup)) {
            deleteTree(partial);
            throw new IOException("cannot stage the previous game data");
        }
        if (!partial.renameTo(target)) {
            if (backup.exists()) backup.renameTo(target);
            deleteTree(partial);
            throw new IOException("cannot activate extracted game data");
        }
        preserveVulkanCache(base, backup);
        deleteTree(backup);
    }

    // Migrate a pre-update cache left inside the old data pack. The native
    // loader still validates the driver identity and contents before use.
    static void preserveVulkanCache(File files, File previousPack) {
        File oldCache = new File(previousPack, "vk_pipeline_cache.bin");
        if (!oldCache.isFile()) return;
        File directory = new File(files, "vulkan-cache");
        if (!directory.isDirectory() && !directory.mkdir()) return;
        File cache = new File(directory, oldCache.getName());
        if (!cache.exists() && !oldCache.renameTo(cache))
            android.util.Log.w("nx2a-game", "Could not move the previous Vulkan pipeline cache");
    }

    private static boolean safeEntry(String name) {
        if (name == null || name.isEmpty() || name.startsWith("/") || name.indexOf('\\') >= 0 || name.indexOf(':') >= 0)
            return false;
        String[] parts = name.split("/");
        for (String part : parts) if (part.equals("..") || part.equals(".")) return false;
        return true;
    }

    private static String digestAsset(Context activity, String name) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new BufferedInputStream(activity.getAssets().open(name))) {
            byte[] b = new byte[64 * 1024];
            int n;
            while ((n = in.read(b)) != -1) if (n > 0) digest.update(b, 0, n);
        }
        StringBuilder out = new StringBuilder(64);
        for (byte b : digest.digest()) out.append(String.format(Locale.ROOT, "%02x", b & 0xff));
        return out.toString();
    }

    private static String readText(File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            byte[] b = new byte[256];
            int n = in.read(b);
            return new String(b, 0, Math.max(0, n), StandardCharsets.US_ASCII);
        }
    }

    static void writeText(File file, String value) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(value.getBytes(StandardCharsets.US_ASCII));
            out.getFD().sync();
        }
    }

    static void copyFile(File from, File to) throws IOException {
        try (InputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(to)) {
            byte[] b = new byte[32 * 1024];
            int n;
            while ((n = in.read(b)) != -1) out.write(b, 0, n);
            out.getFD().sync();
        }
    }

    static void deleteTree(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteTree(child);
        }
        if (file.exists()) file.delete();
    }
}
