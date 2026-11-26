package org.nx2a;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.util.Log;
import android.widget.Toast;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** Scoped-storage document picker; streams an allowlisted report without allocating an archive in RAM. */
final class DiagnosticsExport {
    static final int REQUEST = 0x6e35; // distinct from DriverDocuments' import result
    private DiagnosticsExport() {}

    static void request(Activity activity) {
        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/zip");
                String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
                intent.putExtra(Intent.EXTRA_TITLE, "nx2android-render-diagnostics-" + stamp + ".zip");
                activity.startActivityForResult(intent, REQUEST);
            } catch (RuntimeException | OutOfMemoryError error) {
                startFailed(activity, "Cannot open the document picker", error);
            }
        });
    }

    private static void startFailed(Activity activity, String message, Throwable error) {
        GameActivity.notifyDiagnosticsExport(0);
        Log.e("nx2a-diag", message, error);
        Toast.makeText(activity, message, Toast.LENGTH_LONG).show();
    }

    static void onResult(Activity activity, String root, int code, Intent data) {
        if (code != Activity.RESULT_OK || data == null || data.getData() == null) {
            GameActivity.notifyDiagnosticsExport(2);
            return;
        }
        if (root == null || root.isEmpty()) {
            GameActivity.notifyDiagnosticsExport(0);
            Toast.makeText(activity, "Diagnostic folder unavailable; reopen the game menu", Toast.LENGTH_LONG).show();
            return;
        }
        Uri target = data.getData();
        try {
            new Thread(() -> write(activity, root, target), "nx2a-diagnostic-export").start();
        } catch (RuntimeException | OutOfMemoryError error) {
            startFailed(activity, "Cannot start the diagnostic export", error);
        }
    }

    private static void write(Activity activity, String root, Uri target) {
        int result = 0;
        try {
            String info = "package=" + activity.getPackageName() + "\n" +
                    "manufacturer=" + Build.MANUFACTURER + "\nmodel=" + Build.MODEL +
                    "\nandroid=" + Build.VERSION.RELEASE + "\napi=" + Build.VERSION.SDK_INT +
                    "\nexport_time=" + new Date() + "\n";
            // No app-wide storage permission, game content, or on-disk ZIP copy.
            try (OutputStream stream = activity.getContentResolver().openOutputStream(target, "wt")) {
                if (stream == null) throw new IOException("Destination cannot be opened");
                DiagnosticArchive.write(new File(root), stream, info);
            }
            result = 1;
        } catch (Exception | OutOfMemoryError error) {
            Log.e("nx2a-diag", "Cannot write diagnostics", error);
        } finally {
            GameActivity.notifyDiagnosticsExport(result);
            final int status = result;
            try {
                activity.runOnUiThread(() -> Toast.makeText(activity, status == 1 ?
                        "Diagnostic ZIP exported" : "Diagnostic export failed; select another destination",
                        Toast.LENGTH_LONG).show());
            } catch (RuntimeException error) {
                Log.e("nx2a-diag", "Cannot show export status", error);
            }
        }
    }
}
