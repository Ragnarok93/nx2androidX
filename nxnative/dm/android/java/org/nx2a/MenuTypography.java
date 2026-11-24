package org.nx2a;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.util.Log;

/** Rasterize a tiny immutable system-font atlas once, not on the Vulkan present thread. */
final class MenuTypography {
    private static final int CELL = 48, FIRST = 32, COUNT = 95, BASELINE = 35, LEFT = 3;
    private static boolean installed;
    private static native void installAtlas(byte[] alpha, int[] advances, int cap);
    static synchronized void install() {
        if (installed) return;
        Bitmap bitmap = null;
        try {
            Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
            paint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
            paint.setTextSize(32f);
            paint.setColor(Color.WHITE);
            Rect bounds = new Rect();
            paint.getTextBounds("H", 0, 1, bounds);
            int cap = bounds.height();
            bitmap = Bitmap.createBitmap(CELL, CELL, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bitmap);
            int[] pixels = new int[CELL * CELL], advances = new int[COUNT];
            byte[] alpha = new byte[COUNT * CELL * CELL];
            for (int i = 0; i < COUNT; i++) {
                bitmap.eraseColor(Color.TRANSPARENT);
                String glyph = String.valueOf((char)(FIRST + i));
                canvas.drawText(glyph, LEFT, BASELINE, paint);
                advances[i] = Math.max(1, Math.round(paint.measureText(glyph) * 64f));
                bitmap.getPixels(pixels, 0, CELL, 0, 0, CELL, CELL);
                for (int p = 0; p < pixels.length; p++)
                    alpha[i * CELL * CELL + p] = (byte)(pixels[p] >>> 24);
            }
            installAtlas(alpha, advances, cap);
            installed = true;
        } catch (RuntimeException | UnsatisfiedLinkError error) {
            Log.w("nx2a-menu", "System typography unavailable; using built-in multilingual font", error);
        } finally {
            if (bitmap != null) bitmap.recycle();
        }
    }
    private MenuTypography() {}
}
