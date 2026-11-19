package org.nx2a;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewTreeObserver;
import android.view.ViewGroup;
import android.widget.TextView;
import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

/**
 * Android's system splash stays visible until the launcher draws its first
 * frame. A NativeActivity cannot draw until its game-data check succeeds.
 * Importing from NativeActivity therefore hides the picker/progress dialog
 * behind the splash on newer Android versions.
 *
 * This normal Java launcher draws immediately, prepares the data, and launches
 * the native game only when the pack is actually ready. Nothing is installed
 * separately and nx2android isn't needed at runtime.
 */
public final class BootstrapActivity extends Activity {
    static final String VERIFIED_DATA_PATH = "org.nx2a.VERIFIED_DATA_PATH";
    private boolean launching;
    private boolean firstFrameDrawn;
    private boolean pendingLaunch;
    private String pendingDataPath;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        TextView status = new TextView(this);
        status.setText("Preparing game data…");
        status.setTextSize(18);
        status.setTextColor(Color.WHITE);
        status.setBackgroundColor(Color.rgb(22, 22, 24));
        status.setGravity(Gravity.CENTER);
        status.setPadding(24, 24, 24, 24);
        setContentView(status, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        // Even a cached-data launch must display at least one real frame before
        // handing off. Otherwise finishing here in onCreate can transfer the
        // system splash straight to a NativeActivity that cannot draw yet.
        status.getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener() {
            @Override public boolean onPreDraw() {
                status.getViewTreeObserver().removeOnPreDrawListener(this);
                firstFrameDrawn = true;
                if (pendingLaunch) {
                    String path = pendingDataPath;
                    status.post(() -> launchGame(path));
                }
                return true;
            }
        });
        Log.i("nx2a-bootstrap", "First-launch bootstrap displayed");

        // CompanionImporter will either verify a cached pack and launch, or
        // display a genuine Android folder picker and stream the files.
        if (CompanionImporter.prepare(this)) return;
        if (GameActivity.hasEmbeddedPack(this)) {
            ProgressDialog progress = new ProgressDialog(this);
            progress.setTitle("Preparing game data");
            progress.setMessage("Verifying and extracting bundled files…");
            progress.setIndeterminate(true);
            progress.setCancelable(false);
            progress.show();
            new Thread(() -> {
                try {
                    GameActivity.extractEmbeddedPack(this);
                    runOnUiThread(() -> {
                        if (isFinishing() || isDestroyed()) return;
                        progress.dismiss();
                        launchGame(null);
                    });
                } catch (Exception error) {
                    Log.e("nx2a-bootstrap", "Bundled game data could not be prepared", error);
                    runOnUiThread(() -> {
                        if (isFinishing() || isDestroyed()) return;
                        progress.dismiss();
                        new AlertDialog.Builder(this)
                            .setTitle("Cannot prepare game")
                            .setMessage(error.getMessage() == null ? error.toString() : error.getMessage())
                            .setPositiveButton("Exit", (d, which) -> finish())
                            .setCancelable(false).show();
                    });
                }
            }, "nx2a-bootstrap-extract").start();
            return;
        }
        // Legacy exports without an embedded or companion data marker.
        launchGame(null);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        CompanionImporter.onActivityResult(this, requestCode, resultCode, data);
    }

    void launchGame(String verifiedDataPath) {
        if (launching || isFinishing() || isDestroyed()) return;
        if (!firstFrameDrawn) {
            pendingLaunch = true;
            pendingDataPath = verifiedDataPath;
            return;
        }
        pendingLaunch = false;
        launching = true;
        try {
            String activityName = "org.nx2a.GameActivity";
            try (InputStream in = getAssets().open("nx2a-player-activity");
                 BufferedReader reader = new BufferedReader(
                         new InputStreamReader(in, StandardCharsets.UTF_8))) {
                String supplied = reader.readLine();
                if (supplied != null && supplied.startsWith("org.nx2a.") &&
                        supplied.matches("[A-Za-z0-9._]+"))
                    activityName = supplied;
            } catch (Exception missing) {
                Log.w("nx2a-bootstrap", "Player Activity metadata absent; using default", missing);
            }
            Log.i("nx2a-bootstrap", "Starting native player " + activityName +
                    (verifiedDataPath == null ? " (private data)" : " (verified public data)"));
            Intent intent = new Intent();
            intent.setClassName(getPackageName(), activityName);
            if (verifiedDataPath != null) intent.putExtra(VERIFIED_DATA_PATH, verifiedDataPath);
            startActivity(intent);
            finish();
        } catch (Exception error) {
            launching = false;
            Log.e("nx2a-bootstrap", "Native Activity launch failed", error);
            new AlertDialog.Builder(this).setTitle("Cannot launch game")
                .setMessage(error.getMessage() == null ? error.toString() : error.getMessage())
                .setPositiveButton("Close", (d, which) -> finish())
                .setCancelable(false).show();
        }
    }
}
