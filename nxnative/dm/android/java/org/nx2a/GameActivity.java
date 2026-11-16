package org.nx2a;

import android.app.NativeActivity;
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
    private String extractionError;

    private static String text(byte[] value) {
        return value == null ? "" : new String(value, StandardCharsets.UTF_8);
    }

    // Guest callers wait off the UI thread. Ordinary UTF-8, not JNI modified UTF-8.
    public byte[] readKeyboard(byte[] title, byte[] subtitle, byte[] guide, byte[] initial, int min, int max) {
        return keyboard.read(text(title), text(subtitle), text(guide), text(initial), min, max);
    }

    @Override protected void onCreate(Bundle state) {
        try {
            extractEmbeddedPack();
        } catch (Exception e) {
            extractionError = e.getMessage() == null ? e.toString() : e.getMessage();
            Log.e("nx2a-export", "Could not prepare the bundled game data", e);
        }
        super.onCreate(state);
    }

    @Override protected void onResume() {
        if (extractionError != null) {
            Toast.makeText(this, "Game data extraction failed: " + extractionError, Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        super.onResume();
        keyboard.pause(false);
    }

    @Override protected void onPause() { keyboard.pause(true); super.onPause(); }
    @Override protected void onDestroy() { keyboard.pause(true); super.onDestroy(); }

    private void extractEmbeddedPack() throws Exception {
        // Legacy split-folder APKs do not carry this marker; keep their NativeActivity data-folder flow intact.
        try (InputStream marker = getAssets().open("nx2a-export-mode")) {
            if (marker.read() < 0) throw new IOException("empty export marker");
        } catch (java.io.FileNotFoundException missing) { return; }
        String expected;
        try (InputStream in = getAssets().open("nx2a-data.sha256")) {
            byte[] b = new byte[256];
            int n = in.read(b);
            expected = new String(b, 0, Math.max(0, n), StandardCharsets.US_ASCII).trim().toLowerCase(Locale.ROOT);
        }
        if (!expected.matches("[0-9a-f]{64}")) throw new IOException("invalid embedded data checksum");
        File base = getFilesDir();
        File target = new File(base, "nx2a-data");
        File partial = new File(base, "nx2a-data.partial");
        File backup = new File(base, "nx2a-data.backup");
        File marker = new File(target, "nx2a-export.sha256");
        if (target.isDirectory() && marker.isFile() && expected.equals(readText(marker).trim())) return;

        // Recover a previous directory if the process died between the two rename operations.
        if (!target.exists() && backup.isDirectory()) backup.renameTo(target);
        deleteTree(partial);
        if (!partial.mkdirs() && !partial.isDirectory()) throw new IOException("cannot create private data directory");

        String actual = digestAsset("nx2a-data.zip");
        if (!expected.equals(actual)) throw new IOException("bundled game data checksum mismatch");
        long available = new StatFs(base.getAbsolutePath()).getAvailableBytes();
        long written = 0;
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(getAssets().open("nx2a-data.zip")))) {
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

    private boolean safeEntry(String name) {
        if (name == null || name.isEmpty() || name.startsWith("/") || name.indexOf('\\') >= 0 || name.indexOf(':') >= 0)
            return false;
        String[] parts = name.split("/");
        for (String part : parts) if (part.equals("..") || part.equals(".")) return false;
        return true;
    }

    private String digestAsset(String name) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new BufferedInputStream(getAssets().open(name))) {
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

    private static void writeText(File file, String value) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(value.getBytes(StandardCharsets.US_ASCII));
            out.getFD().sync();
        }
    }

    private static void copyFile(File from, File to) throws IOException {
        try (InputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(to)) {
            byte[] b = new byte[32 * 1024];
            int n;
            while ((n = in.read(b)) != -1) out.write(b, 0, n);
            out.getFD().sync();
        }
    }

    private static void deleteTree(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteTree(child);
        }
        if (file.exists()) file.delete();
    }
}
