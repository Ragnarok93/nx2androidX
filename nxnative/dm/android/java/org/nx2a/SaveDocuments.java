package org.nx2a;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Log;
import android.widget.Toast;
import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/** SAF save ZIP import/export. No broad storage permission or filesystem path from a document URI. */
final class SaveDocuments {
    private static final String TAG = "nx2a-saves";
    private static final int EXPORT = 0x6e41;
    private static final int IMPORT = 0x6e42;
    private static final long MAX_IMPORT_BYTES = 2L * 1024 * 1024 * 1024;
    private static final int MAX_IMPORT_ENTRIES = 100000;
    private static final int BUF = 64 * 1024;

    private SaveDocuments() {}

    static void chooseExport(GameActivity a) {
        a.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("application/zip");
            String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
            i.putExtra(Intent.EXTRA_TITLE, "nx2android-save-" + stamp + ".zip");
            try { a.startActivityForResult(i, EXPORT); }
            catch (Exception e) { fail(a, "Cannot open export picker", e); }
        });
    }

    static void chooseImport(GameActivity a) {
        a.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("application/zip");
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            try { a.startActivityForResult(i, IMPORT); }
            catch (Exception e) { fail(a, "Cannot open import picker", e); }
        });
    }

    static boolean onActivityResult(GameActivity a, int request, int result, Intent data) {
        if (request != EXPORT && request != IMPORT) return false;
        if (result != Activity.RESULT_OK || data == null || data.getData() == null) return true;
        Uri uri = data.getData();
        if (request == EXPORT)
            new Thread(() -> export(a, uri), "nx2a-save-export").start();
        else
            new Thread(() -> importZip(a, uri), "nx2a-save-import").start();
        return true;
    }

    private static File saves(GameActivity a) throws IOException {
        File root = a.getExternalFilesDir(null);
        if (root == null) throw new IOException("App's external files directory is unavailable");
        File saves = new File(root, "saves");
        if (!saves.isDirectory() && !saves.mkdirs()) throw new IOException("Cannot create saves directory");
        return saves;
    }

    private static void export(GameActivity a, Uri uri) {
        boolean success = false;
        String note = "Export failed";
        try {
            String name = GameActivity.saveExportSnapshot();
            if (name == null || name.length() == 0) throw new IOException("The save snapshot is unavailable");
            if (name.startsWith("!")) throw new IOException(name.substring(1));
            File root = saves(a);
            File game = new File(root, name);
            if (!game.isDirectory()) throw new IOException("Missing exported save " + name);
            try (OutputStream stream = a.getContentResolver().openOutputStream(uri, "w")) {
                if (stream == null) throw new IOException("The selected destination is not writable");
                try (ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(stream))) {
                    addTree(zip, game, name + "/");
                    File device = new File(root, name + ".device");
                    if (device.isDirectory()) addTree(zip, device, name + ".device/");
                    zip.finish();
                }
            }
            success = true;
            note = "Saved ZIP";
            notify(a, "Save exported to the selected document");
        } catch (Exception e) {
            Log.e(TAG, "Save export failed", e);
            note = e.getMessage() == null ? "Export failed" : e.getMessage();
            try { DocumentsContract.deleteDocument(a.getContentResolver(), uri); }
            catch (Exception ignored) { /* Provider may not support deletion. */ }
            notify(a, "Save export failed: " + note);
        } finally {
            GameActivity.saveExportStatus(success, note);
        }
    }

    private static void addTree(ZipOutputStream zip, File root, String prefix) throws IOException {
        File[] children = root.listFiles();
        if (children == null) throw new IOException("Cannot read " + root);
        Arrays.sort(children, (x, y) -> x.getName().compareTo(y.getName()));
        for (File f : children) {
            if (java.nio.file.Files.isSymbolicLink(f.toPath())) throw new IOException("Refusing symbolic link in save");
            String entry = prefix + f.getName();
            if (f.isDirectory()) addTree(zip, f, entry + "/");
            else if (f.isFile()) {
                zip.putNextEntry(new ZipEntry(entry));
                try (InputStream in = new BufferedInputStream(new FileInputStream(f))) { copy(in, zip); }
                zip.closeEntry();
            }
        }
    }

    private static void importZip(GameActivity a, Uri uri) {
        File temp = null, primary = null, device = null;
        boolean staged = false;
        try {
            File root = saves(a);
            String name = "import-" + System.currentTimeMillis();
            for (int suffix = 2; new File(root, name).exists() || new File(root, "." + name + ".part").exists(); suffix++)
                name = "import-" + System.currentTimeMillis() + "-" + suffix;
            temp = new File(root, "." + name + ".part");
            if (!temp.mkdir()) throw new IOException("Cannot create temporary save directory");
            long total = 0;
            int entries = 0;
            Set<String> seen = new HashSet<>();
            try (InputStream stream = a.getContentResolver().openInputStream(uri)) {
                if (stream == null) throw new IOException("Cannot read selected ZIP document");
                try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(stream))) {
                    ZipEntry entry;
                    byte[] buffer = new byte[BUF];
                    while ((entry = zip.getNextEntry()) != null) {
                        if (++entries > MAX_IMPORT_ENTRIES) throw new IOException("Too many files in save ZIP");
                        String path = entry.getName();
                        if (path.isEmpty() || path.startsWith("/") || path.contains("\\")
                                || path.indexOf('\0') >= 0 || path.matches("^[A-Za-z]:.*"))
                            throw new IOException("Unsafe ZIP path");
                        File f = new File(temp, path);
                        String base = temp.getCanonicalPath() + File.separator;
                        if (!f.getCanonicalPath().startsWith(base) || !seen.add(f.getCanonicalPath()))
                            throw new IOException("Unsafe or repeated ZIP entry");
                        if (entry.isDirectory()) {
                            if (!f.isDirectory() && !f.mkdirs()) throw new IOException("Cannot create save folder");
                        } else {
                            File dir = f.getParentFile();
                            if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("Cannot create save folder");
                            try (FileOutputStream out = new FileOutputStream(f)) {
                                int n;
                                while ((n = zip.read(buffer)) != -1) {
                                    total += n;
                                    if (total > MAX_IMPORT_BYTES) throw new IOException("Save ZIP exceeds 2 GiB");
                                    out.write(buffer, 0, n);
                                }
                                out.getFD().sync();
                            }
                        }
                        zip.closeEntry();
                    }
                }
            }
            if (total == 0) throw new IOException("The selected ZIP contains no save files");
            // Our own exports contain one named save root and optionally its paired .device root.
            // Archives of raw console save files can instead contain their files at ZIP root.
            File content = temp, devContent = null;
            File[] top = temp.listFiles();
            if (top != null && top.length >= 1 && top.length <= 2) {
                File candidate = null, paired = null;
                for (File f : top) {
                    if (f.isDirectory() && f.getName().endsWith(".device")) paired = f;
                    else if (f.isDirectory()) candidate = f;
                }
                if (candidate != null && (top.length == 1 || (paired != null
                        && paired.getName().equals(candidate.getName() + ".device")))) {
                    content = candidate;
                    devContent = paired;
                }
            }
            primary = new File(root, name);
            device = new File(root, name + ".device");
            if (devContent != null && !devContent.renameTo(device))
                throw new IOException("Cannot activate device save");
            if (!content.renameTo(primary)) throw new IOException("Cannot activate imported save");
            if (!GameActivity.saveImportStage(name)) throw new IOException("Cannot schedule save import");
            staged = true;
            notify(a, "Save selected. Game will close; reopen it to import.");
            a.runOnUiThread(a::finish);
        } catch (Exception e) {
            Log.e(TAG, "Save import failed", e);
            notify(a, "Save import failed: " + (e.getMessage() == null ? "Invalid ZIP" : e.getMessage()));
        } finally {
            if (temp != null) deleteTree(temp);
            if (!staged) {
                if (primary != null) deleteTree(primary);
                if (device != null) deleteTree(device);
            }
        }
    }

    private static long copy(InputStream src, OutputStream dst) throws IOException {
        byte[] b = new byte[BUF];
        long n = 0;
        int k;
        while ((k = src.read(b)) != -1) { dst.write(b, 0, k); n += k; }
        return n;
    }

    private static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        File[] children = f.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        if (!f.delete()) Log.w(TAG, "Could not remove " + f);
    }

    private static void notify(GameActivity a, String msg) {
        a.runOnUiThread(() -> Toast.makeText(a, msg, Toast.LENGTH_LONG).show());
    }
    private static void fail(GameActivity a, String msg, Exception e) {
        Log.e(TAG, msg, e);
        notify(a, msg + ": " + e.getMessage());
    }
}
