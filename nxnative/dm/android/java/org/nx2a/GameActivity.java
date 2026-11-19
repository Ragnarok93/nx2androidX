package org.nx2a;

import android.app.NativeActivity;
import android.app.Activity;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
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
    private final GameKeyboard keyboard = new GameKeyboard(this);

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
        super.onCreate(state);
        prepareEmbeddedPack(this);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        CompanionImporter.onActivityResult(this, requestCode, resultCode, data);
    }

    @Override protected void onResume() {
        super.onResume();
        keyboard.pause(false);
    }

    private static boolean hasEmbeddedPack(Context context) {
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
        deleteTree(backup);
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
