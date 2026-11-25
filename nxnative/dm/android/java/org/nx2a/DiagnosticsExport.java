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
    static final int REQUEST = 0x6e34;
    private DiagnosticsExport() {}

    static void request(Activity activity) {
        activity.runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/zip");
            String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date());
            intent.putExtra(Intent.EXTRA_TITLE, "nx2android-render-diagnostics-" + stamp + ".zip");
            try { activity.startActivityForResult(intent, REQUEST); }
            catch (Exception error) {
                Log.e("nx2a-diag", "Cannot open Android document picker", error);
                GameActivity.notifyDiagnosticsExport(0);
                Toast.makeText(activity, "Cannot export: " + error.getMessage(), Toast.LENGTH_LONG).show();
            }
        });
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
        new Thread(() -> {
            if (!GameActivity.diagnosticsExportStarted()) return;
            int result = 1;
            try {
                String info = "package=" + activity.getPackageName() + "\n" +
                        "manufacturer=" + Build.MANUFACTURER + "\nmodel=" + Build.MODEL +
                        "\nandroid=" + Build.VERSION.RELEASE + "\napi=" + Build.VERSION.SDK_INT +
                        "\nexport_time=" + new Date().toString() + "\n";
                // No app-wide storage permission, no game content and no on-disk ZIP copy.
                try (OutputStream stream = activity.getContentResolver().openOutputStream(target, "wt")) {
                    if (stream == null) throw new IOException("Destination cannot be opened");
                    DiagnosticArchive.write(new File(root), stream, info);
                }
            } catch (Exception error) {
                result = 0;
                Log.e("nx2a-diag", "Cannot write diagnostics", error);
            }
            final int status = result;
            activity.runOnUiThread(() -> {
                GameActivity.notifyDiagnosticsExport(status);
                Toast.makeText(activity, status == 1 ? "Diagnostic ZIP exported" :
                        "Diagnostic export failed; select another destination", Toast.LENGTH_LONG).show();
            });
        }, "nx2a-diagnostic-export").start();
    }
}
