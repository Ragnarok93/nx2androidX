package org.nx2a;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.StatFs;
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
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;
import org.json.JSONObject;

/**
 * First-run companion data importer for large independently installable game APKs.
 * No raw access to another app's Android/data folder, no ZIP64 APK, no nx2android
 * dependency, and no third-party file picker. Works with document providers and
 * long (>4 GiB) individual game assets via streaming FileOutputStream writes.
 */
final class CompanionImporter {
    private static final int REQUEST_FOLDER = 0x6e32;
    private static final String DESCRIPTOR = "nx2a-companion.json";
    private static final String MANIFEST = "nx2a-data.json";
    private static final String RECEIPT = "nx2a-companion.sha256";
    private static final long MARGIN = 128L << 20;

    private CompanionImporter() { }

    private static JSONObject descriptor(Activity activity) throws Exception {
        try (InputStream in = activity.getAssets().open(DESCRIPTOR)) {
            byte[] b = new byte[4096];
            int n = in.read(b);
            if (n <= 0) throw new IOException("empty game-data descriptor");
            JSONObject info = new JSONObject(new String(b, 0, n, StandardCharsets.UTF_8));
            String folder = info.getString("folder");
            String hash = info.getString("manifest_sha256");
            if (info.getInt("schema") != 1 || !safeName(folder) ||
                    !hash.matches("[a-f0-9]{64}") || info.getLong("bytes") < 0)
                throw new IOException("invalid game-data descriptor");
            return info;
        }
    }

    private static boolean exists(Activity activity) {
        try (InputStream in = activity.getAssets().open(DESCRIPTOR)) {
            return in.read() >= 0;
        } catch (java.io.FileNotFoundException missing) {
            return false;
        } catch (IOException error) {
            // Malformed descriptors must show an error, not silently run the native host.
            return true;
        }
    }

    static boolean prepare(Activity activity) {
        if (!exists(activity)) return false;
        final JSONObject info;
        try {
            info = descriptor(activity);
            File root = new File(activity.getFilesDir(), "nx2a-data");
            if (cached(root, info.getString("manifest_sha256"))) {
                GameActivity.embeddedPackReady();
                return true;
            }
        } catch (Exception error) {
            failure(activity, "Cannot read game-data instructions", error);
            return true;
        }
        new AlertDialog.Builder(activity)
            .setTitle("Import game data")
            .setMessage("This game is larger than one APK can hold. Select its exported " +
                    info.optString("folder") + " folder. The game will verify and import " +
                    "its files once, without needing nx2android installed. " +
                    "You need enough free storage for the imported data.")
            .setPositiveButton("Choose data folder", (d, which) -> {
                Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                try {
                    activity.startActivityForResult(picker, REQUEST_FOLDER);
                } catch (Exception error) {
                    failure(activity, "Cannot open the system folder picker", error);
                }
            })
            .setNegativeButton("Exit", (d, which) -> activity.finish())
            .setCancelable(false).show();
        return true;
    }

    static void onActivityResult(Activity activity, int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_FOLDER) return;
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) {
            activity.finish();
            return;
        }
        Uri tree = data.getData();
        try {
            activity.getContentResolver().takePersistableUriPermission(
                    tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
            // The picker also grants a temporary read permission for this import.
        }
        ProgressDialog progress = new ProgressDialog(activity);
        progress.setTitle("Importing game data");
        progress.setMessage("Checking selected folder…");
        progress.setIndeterminate(true);
        progress.setCancelable(false);
        progress.show();
        new Thread(() -> {
            try {
                JSONObject info = descriptor(activity);
                importFolder(activity, tree, info, progress);
                activity.runOnUiThread(() -> {
                    if (activity.isFinishing() || activity.isDestroyed()) return;
                    progress.dismiss();
                    GameActivity.embeddedPackReady();
                });
            } catch (Exception error) {
                Log.e("nx2a-companion", "Game-data import failed", error);
                activity.runOnUiThread(() -> {
                    if (activity.isFinishing() || activity.isDestroyed()) return;
                    progress.dismiss();
                    new AlertDialog.Builder(activity)
                        .setTitle("Game data not ready")
                        .setMessage(error.getMessage() == null ? error.toString() : error.getMessage())
                        .setPositiveButton("Choose folder again", (d, which) -> {
                            Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                            picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                            activity.startActivityForResult(picker, REQUEST_FOLDER);
                        })
                        .setNegativeButton("Exit", (d, which) -> activity.finish())
                        .setCancelable(false).show();
                });
            }
        }, "nx2a-companion-import").start();
    }

    private static boolean cached(File root, String expected) throws Exception {
        File receipt = new File(root, RECEIPT);
        File manifest = new File(root, MANIFEST);
        return root.isDirectory() && receipt.isFile() && manifest.isFile() &&
                expected.equals(readText(receipt).trim()) &&
                expected.equals(digestFile(manifest));
    }

    private static Uri child(Activity activity, Uri tree, Uri parent, String name,
                             boolean directory) throws Exception {
        ContentResolver resolver = activity.getContentResolver();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                tree, DocumentsContract.getDocumentId(parent));
        try (Cursor rows = resolver.query(children,
                new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                        DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            if (rows == null) throw new IOException("Cannot list selected data folder");
            while (rows.moveToNext()) {
                if (name.equals(rows.getString(1)) &&
                        (DocumentsContract.Document.MIME_TYPE_DIR.equals(rows.getString(2)) == directory))
                    return DocumentsContract.buildDocumentUriUsingTree(tree, rows.getString(0));
            }
        }
        return null;
    }

    private static Uri selectFolder(Activity activity, Uri tree, JSONObject info) throws Exception {
        Uri root = DocumentsContract.buildDocumentUriUsingTree(
                tree, DocumentsContract.getTreeDocumentId(tree));
        if (child(activity, tree, root, MANIFEST, false) != null) return root;
        Uri nested = child(activity, tree, root, info.getString("folder"), true);
        if (nested != null && child(activity, tree, nested, MANIFEST, false) != null)
            return nested;
        throw new IOException("Select the exported " + info.getString("folder") +
                " directory, or the parent directory containing it.");
    }

    private static String digestFile(File file) throws Exception {
        try (InputStream in = new FileInputStream(file)) { return digest(in); }
    }

    private static String digest(InputStream input) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        byte[] buffer = new byte[1 << 16];
        int n;
        while ((n = input.read(buffer)) != -1) if (n > 0) md.update(buffer, 0, n);
        StringBuilder result = new StringBuilder(64);
        for (byte b : md.digest()) result.append(String.format(Locale.US, "%02x", b & 0xff));
        return result.toString();
    }

    private static boolean safeName(String name) {
        return name != null && !name.isEmpty() && name.length() <= 255 &&
                !name.equals(".") && !name.equals("..") &&
                name.indexOf('/') < 0 && name.indexOf('\\') < 0 &&
                name.indexOf(':') < 0 && name.indexOf('\0') < 0;
    }

    private static final class CopyState {
        final Activity activity;
        final ProgressDialog dialog;
        final long expected;
        final long allowance;
        long written;
        long lastReported;
        CopyState(Activity activity, ProgressDialog dialog, long expected) {
            this.activity = activity;
            this.dialog = dialog;
            this.expected = expected;
            this.allowance = expected > Long.MAX_VALUE - MARGIN ? Long.MAX_VALUE : expected + MARGIN;
        }
        void add(int bytes) throws IOException {
            if (written > allowance - bytes)
                throw new IOException("Selected folder contains more data than this game export expects");
            written += bytes;
            if (written - lastReported >= (64L << 20)) {
                lastReported = written;
                long count = written;
                activity.runOnUiThread(() -> dialog.setMessage(
                    String.format(Locale.US, "Importing %.2f / %.2f GiB",
                        count / 1073741824.0, expected / 1073741824.0)));
            }
        }
    }

    private static void copyTree(Activity activity, Uri tree, Uri src, File dst,
                                 CopyState state, int depth) throws Exception {
        if (depth > 64) throw new IOException("Game-data folder nesting is too deep");
        if (!dst.isDirectory() && !dst.mkdirs()) throw new IOException("Cannot create " + dst);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                tree, DocumentsContract.getDocumentId(src));
        try (Cursor rows = activity.getContentResolver().query(children,
                new String[]{DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                        DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            if (rows == null) throw new IOException("Could not read folder " + dst.getName());
            while (rows.moveToNext()) {
                String name = rows.getString(1);
                if (!safeName(name)) throw new IOException("Unsafe file name in selected folder");
                File next = new File(dst, name);
                if (!next.getCanonicalPath().startsWith(dst.getCanonicalPath() + File.separator))
                    throw new IOException("Unsafe game-data file path");
                Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, rows.getString(0));
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(rows.getString(2))) {
                    copyTree(activity, tree, uri, next, state, depth + 1);
                } else {
                    try (InputStream raw = activity.getContentResolver().openInputStream(uri)) {
                        if (raw == null) throw new IOException("Cannot open " + name);
                        try (InputStream input = new BufferedInputStream(raw);
                             BufferedOutputStream output = new BufferedOutputStream(new FileOutputStream(next))) {
                            byte[] buffer = new byte[1 << 20];
                            int n;
                            while ((n = input.read(buffer)) != -1) {
                                if (n == 0) continue;
                                state.add(n);
                                output.write(buffer, 0, n);
                            }
                        }
                    }
                }
            }
        }
    }

    private static void importFolder(Activity activity, Uri tree, JSONObject info,
                                     ProgressDialog progress) throws Exception {
        String expected = info.getString("manifest_sha256");
        Uri source = selectFolder(activity, tree, info);
        Uri manifest = child(activity, tree, source, MANIFEST, false);
        if (manifest == null) throw new IOException("Selected folder has no game manifest");
        try (InputStream in = activity.getContentResolver().openInputStream(manifest)) {
            if (in == null || !expected.equals(digest(in)))
                throw new IOException("That data folder does not match this exported APK");
        }
        File base = activity.getFilesDir();
        File target = new File(base, "nx2a-data");
        File partial = new File(base, "nx2a-data.partial");
        File backup = new File(base, "nx2a-data.backup");
        long bytes = info.getLong("bytes");
        long free = new StatFs(base.getAbsolutePath()).getAvailableBytes();
        if (free < bytes || free - bytes < (64L << 20))
            throw new IOException(String.format(Locale.US,
                "Not enough free storage for game data: %.2f GiB required, %.2f GiB available",
                (bytes + (64L << 20)) / 1073741824.0, free / 1073741824.0));
        GameActivity.deleteTree(partial);
        if (!partial.mkdirs()) throw new IOException("Cannot create temporary game-data folder");
        try {
            CopyState state = new CopyState(activity, progress, bytes);
            copyTree(activity, tree, source, partial, state, 0);
            File copied = new File(partial, MANIFEST);
            if (!copied.isFile() || !expected.equals(digestFile(copied)))
                throw new IOException("Game-data manifest changed or is incomplete");
            File existingSettings = new File(target, "settings.txt");
            if (existingSettings.isFile())
                GameActivity.copyFile(existingSettings, new File(partial, "settings.txt"));
            GameActivity.writeText(new File(partial, RECEIPT), expected + "\n");
            GameActivity.deleteTree(backup);
            if (target.exists() && !target.renameTo(backup))
                throw new IOException("Cannot back up previous game data");
            if (!partial.renameTo(target)) {
                if (backup.exists()) backup.renameTo(target);
                throw new IOException("Cannot activate imported game data");
            }
            GameActivity.deleteTree(backup);
        } finally {
            GameActivity.deleteTree(partial);
        }
    }

    private static String readText(File file) throws Exception {
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[128];
            int n = in.read(buffer);
            return n < 0 ? "" : new String(buffer, 0, n, StandardCharsets.US_ASCII);
        }
    }

    private static void failure(Activity activity, String title, Exception error) {
        Log.e("nx2a-companion", title, error);
        new AlertDialog.Builder(activity).setTitle(title)
            .setMessage(error.getMessage() == null ? error.toString() : error.getMessage())
            .setPositiveButton("Close", (d, which) -> activity.finish()).setCancelable(false).show();
    }
}
