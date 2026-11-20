package org.nx2a;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Standalone game diagnostic export. No Android dependency: exercised by a host Java fixture.
 * Only explicit, non-recursive diagnostic files are included: NEVER RomFS, shaders,
 * keys, saves, mods, the driver's .so, or user-imported DLLs.
 */
final class DiagnosticArchive {
    private DiagnosticArchive() {}
    static final long LOG_TAIL = 8L << 20;     // per run; keep the newest error output
    static final long LOG_HEAD = 64L << 10;    // retain device/driver startup lines too
    static final long FILE_LIMIT = 128L << 20;
    static final long TOTAL_LIMIT = 384L << 20;
    static final int ENTRY_LIMIT = 310;        // 96 x (preview, raw, floats) + metadata
    static boolean allowedName(String name) {
        return name.equals("log.txt") || name.equals("log.prev.txt") ||
               name.equals("log.prev.2.txt") || name.equals("log.prev.3.txt") ||
               name.equals("capture_meta.txt") || name.equals("pass_dump_manifest.tsv") ||
               name.equals("gpu_draw_inputs.tsv") ||
               name.matches("pass_[0-9]{2}\\.ppm(?:\\.f32)?") ||
               name.matches("pass_[0-9]{2}\\.raw") ||
               name.matches("frame_[0-9]{6}\\.ppm");
    }
    private static boolean logName(String name) {
        return name.startsWith("log") && name.endsWith(".txt");
    }
    private static String entryName(String name) {
        return logName(name) ? "logs/" + name : name.equals("capture_meta.txt") ||
               name.equals("pass_dump_manifest.tsv") || name.equals("gpu_draw_inputs.tsv")
               ? "metadata/" + name : "captures/" + name;
    }
    private static void entry(ZipOutputStream zip, String name, byte[] data) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(data);
        zip.closeEntry();
    }
    private static void copy(ZipOutputStream zip, String name, File file, long from, long len) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        try (FileInputStream input = new FileInputStream(file)) {
            // FileInputStream.skip is permitted to return fewer bytes than requested.
            input.getChannel().position(from);
            byte[] buffer = new byte[64 * 1024];  // bounded irrespective of capture size
            while (len > 0) {
                int n = input.read(buffer, 0, (int)Math.min((long)buffer.length, len));
                if (n < 0) throw new IOException("File shortened while exporting: " + file.getName());
                if (n == 0) continue;
                zip.write(buffer, 0, n);
                len -= n;
            }
        }
        zip.closeEntry();
    }
    static void write(File directory, OutputStream out, String systemInfo) throws IOException {
        File root = directory.getCanonicalFile();
        if (!root.isDirectory()) throw new IOException("Game diagnostic folder is unavailable");
        File[] children = root.listFiles();
        if (children == null) throw new IOException("Cannot list diagnostic folder");
        // The manifest, not a stale pass_%02d file left by an earlier
        // capture, identifies which attachments belong to this capture.
        Set<String> currentPasses = new HashSet<>();
        File manifest = new File(root, "pass_dump_manifest.tsv");
        File captureInfo = new File(root, "capture_meta.txt");
        boolean restrictPasses = manifest.isFile() || captureInfo.isFile();
        if (manifest.isFile() && (!captureInfo.isFile() ||
                manifest.lastModified() >= captureInfo.lastModified())) {
            try (BufferedReader lines = Files.newBufferedReader(manifest.toPath(), StandardCharsets.UTF_8)) {
                String line;
                int seen = 0;
                while ((line = lines.readLine()) != null && seen++ < 97) {
                    int tab = line.indexOf('\t');
                    if (tab <= 0) continue;
                    try {
                        int index = Integer.parseInt(line.substring(0, tab));
                        if (index >= 0 && index < 96)
                            currentPasses.add(String.format(java.util.Locale.US, "pass_%02d", index));
                    } catch (NumberFormatException ignored) {
                        // Header is not an image index.
                    }
                }
            }
        }
        List<File> inputs = new ArrayList<>();
        String prefix = root.getPath() + File.separator;
        for (File f : children) {
            if (!allowedName(f.getName()) || !f.isFile() || Files.isSymbolicLink(f.toPath())) continue;
            if (restrictPasses && f.getName().startsWith("pass_") &&
                    !f.getName().equals("pass_dump_manifest.tsv") &&
                    !currentPasses.contains(f.getName().substring(0, 7))) continue;
            if (f.getName().equals("gpu_draw_inputs.tsv") &&
                    (!manifest.isFile() || (captureInfo.isFile() &&
                    f.lastModified() < captureInfo.lastModified()))) continue;
            if (!f.getCanonicalPath().startsWith(prefix)) continue;
            inputs.add(f);
        }
        inputs.sort(Comparator.comparing(File::getName));
        if (inputs.size() > ENTRY_LIMIT) throw new IOException("Diagnostic file count exceeds safe limit");
        long total = 0;
        int collected = 0;
        StringBuilder skipped = new StringBuilder();
        // The only archive created is one the player selected with ACTION_CREATE_DOCUMENT.
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            entry(zip, "README.txt", ("nx2androidX standalone rendering diagnostics\n" +
                    "gpu_draw_inputs.tsv contains completed GPU buffer copies taken after read-only passes.\n" +
                    "Index and vertex rows are binding prefixes. Rasterization requires separate evidence.\n" +
                    "CPU/shadow indirect diagnostics are explicitly non-authoritative.\n" +
                    "High-overhead full MRT/depth/stencil readback is OFF unless Capture is pressed.\n" +
                    "The last three prior runtime logs are retained across app launches.\n" +
                    "No game ROM, shaders, saves or user DLLs are included.\n" +
                    "Logs may contain device identifiers, filesystem paths, shader keys and GPU addresses.\n" +
                    "Review the report before sharing it.\n\n" + systemInfo + "\n")
                    .getBytes(StandardCharsets.UTF_8));
            for (File file : inputs) {
                long length = file.length();
                if (length <= 0) continue;
                boolean log = logName(file.getName());
                long count = log ? Math.min(length, LOG_TAIL) : length;
                if (count > FILE_LIMIT || count > TOTAL_LIMIT - total) {
                    skipped.append(file.getName()).append(": exceeded export byte budget\n");
                    continue;
                }
                long start = log ? length - count : 0;
                copy(zip, entryName(file.getName()), file, start, count);
                total += count;
                collected++;
                if (log && length > count && LOG_HEAD <= TOTAL_LIMIT - total) {
                    copy(zip, "logs/" + file.getName() + ".startup.txt", file, 0, Math.min(length, LOG_HEAD));
                    total += Math.min(length, LOG_HEAD);
                }
            }
            entry(zip, "archive-status.txt", ("files=" + collected + "\nsource_bytes=" + total +
                    "\nsource_limit=" + TOTAL_LIMIT + "\n" +
                    (skipped.length() == 0 ? "skipped=none\n" : skipped.toString()))
                    .getBytes(StandardCharsets.UTF_8));
        }
    }
}
