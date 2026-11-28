package org.nx2a;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
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
    static final int ENTRY_LIMIT = 1240;       // legacy files plus three completed rolling captures
    private static boolean metadataName(String name) {
        return name.equals("capture_timeline.tsv") || name.equals("scaling_frames.tsv") ||
               name.equals("capture_meta.txt") || name.equals("pass_dump_manifest.tsv") ||
               name.equals("gpu_draw_inputs.tsv") || name.equals("surface_usage.tsv") ||
               name.equals("surface_samples.tsv") || name.equals("temporal-shaders.json");
    }
    static boolean allowedName(String name) {
        return name.equals("log.txt") || name.equals("log.prev.txt") ||
               name.equals("log.prev.2.txt") || name.equals("log.prev.3.txt") ||
               metadataName(name) || name.equals("surface_samples.raw") ||
               name.matches("pass_[0-9]{2}\\.ppm(?:\\.f32)?") ||
               name.matches("pass_[0-9]{2}\\.raw") ||
               name.matches("frame_[0-9]{6}\\.ppm");
    }
    private static boolean logName(String name) {
        return name.startsWith("log") && name.endsWith(".txt");
    }
    private static int exportPriority(File file) {
        String name = file.getName();
        if (metadataName(name)) return 0;
        if (logName(name)) return 1;
        if (name.endsWith(".raw")) return 2;
        return 3;
    }
    private static String entryName(String name) {
        return logName(name) ? "logs/" + name :
               metadataName(name) ? "metadata/" + name : "captures/" + name;
    }
    private static void entry(ZipOutputStream zip, String name, byte[] data) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(data);
        zip.closeEntry();
    }
    private static long copy(ZipOutputStream zip, String name, FileInputStream input,
                             long from, long len, byte[] buffer) throws IOException {
        input.getChannel().position(from);
        zip.putNextEntry(new ZipEntry(name));
        long remaining = len;
        while (remaining > 0) {
            int n = input.read(buffer, 0, (int)Math.min((long)buffer.length, remaining));
            if (n < 0) break;
            if (n == 0) continue;
            zip.write(buffer, 0, n);
            remaining -= n;
        }
        zip.closeEntry();
        return len - remaining;
    }
    private static boolean currentFile(File f, long captureTime) {
        return f.isFile() && !Files.isSymbolicLink(f.toPath()) && f.length() > 0 &&
               f.lastModified() >= captureTime;
    }
    private static final class ArchiveInput {
        final File file;
        final String prefix;
        ArchiveInput(File file, String prefix) { this.file = file; this.prefix = prefix; }
    }
    private static void collect(File root, String archivePrefix, List<ArchiveInput> inputs) throws IOException {
        File[] children = root.listFiles();
        if (children == null) throw new IOException("Cannot list diagnostic folder");
        // The manifest, not a stale pass_%02d file left by an earlier
        // capture, identifies which attachments belong to this capture.
        Set<String> currentPasses = new HashSet<>();
        File manifest = new File(root, "pass_dump_manifest.tsv");
        File captureInfo = new File(root, "capture_meta.txt");
        boolean restrictPasses = !archivePrefix.isEmpty() || manifest.isFile() || captureInfo.isFile();
        long captureTime = captureInfo.isFile() ? captureInfo.lastModified() : 0;
        boolean manifestCurrent = manifest.isFile() && manifest.lastModified() >= captureTime;
        File catalog = new File(root, "surface_usage.tsv");
        File samples = new File(root, "surface_samples.tsv");
        File raw = new File(root, "surface_samples.raw");
        boolean catalogCurrent = currentFile(catalog, captureTime);
        boolean surfacesCompleted = catalogCurrent && currentFile(samples, captureTime) &&
                                    currentFile(raw, captureTime);
        if (manifestCurrent) {
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
        String prefix = root.getPath() + File.separator;
        for (File f : children) {
            if (!allowedName(f.getName()) || !f.isFile() || Files.isSymbolicLink(f.toPath())) continue;
            String name = f.getName();
            if (name.equals("pass_dump_manifest.tsv") && !manifestCurrent) continue;
            if (restrictPasses && name.startsWith("pass_") &&
                    !name.equals("pass_dump_manifest.tsv") &&
                    (!currentPasses.contains(name.substring(0, 7)) || f.lastModified() < captureTime)) continue;
            if (f.getName().equals("gpu_draw_inputs.tsv") &&
                    (!(manifestCurrent || catalogCurrent) || f.lastModified() < captureTime)) continue;
            if (name.equals("surface_usage.tsv") && !catalogCurrent) continue;
            if ((name.equals("surface_samples.tsv") || name.equals("surface_samples.raw")) &&
                    !surfacesCompleted) continue;
            if (!archivePrefix.isEmpty() && (logName(name) || name.startsWith("frame_") || name.equals("temporal-shaders.json") || name.equals("scaling_frames.tsv"))) continue;
            if (!f.getCanonicalPath().startsWith(prefix)) continue;
            inputs.add(new ArchiveInput(f, archivePrefix));
        }
    }
    static void write(File directory, OutputStream out, String systemInfo) throws IOException {
        File root = directory.getCanonicalFile();
        if (!root.isDirectory()) throw new IOException("Game diagnostic folder is unavailable");
        List<ArchiveInput> inputs = new ArrayList<>();
        collect(root, "", inputs);
        for (int slot = 0; slot < 3; slot++) {
            File path = new File(root, "diagnostic-capture-" + slot);
            if (!path.isDirectory() || Files.isSymbolicLink(path.toPath())) continue;
            File complete = new File(path, "complete");
            if (!complete.isFile() || Files.isSymbolicLink(complete.toPath())) continue;
            collect(path, "history/slot-" + slot + "/", inputs);
        }
        // Keep the manifest, logs and exact readbacks ahead of redundant
        // previews so a cut-off transfer can still be matched to the capture.
        inputs.sort(Comparator.comparingInt((ArchiveInput input) -> exportPriority(input.file))
                              .thenComparing(input -> input.prefix + input.file.getName()));
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
                    "Performance debug level records scaling, reconstruction and LSFG metadata without GPU readbacks. Detailed also records capture_timeline.tsv without GPU readbacks.\n" +
                    "scaling_frames.tsv retains up to 4096 NVN source-frame tickets and snapshots the trace when export begins.\n" +
                    "Scaled dimensions are from the largest recorded scaled pass. Presentation shows the evaluated input, fitted output and active filter.\n" +
                    "A physical candidate may be bypassed. When the active filter is None, ordinary presentation handles the frame.\n" +
                    "Replay GPU time is the most recent available sample (-1 if missing). Reconstruction time has its own timing ticket (0 if missing).\n" +
                    "GPU snapshots adds full attachment readbacks to Detailed. Snapshot frames can affect scaling and timing.\n" +
                    "history contains up to three completed full-frame snapshots with present numbers in their manifests.\n" +
                    "surface_usage.tsv lists attachment, shader image, transfer and presentation bindings.\n" +
                    "Alias rows pair the sampled view with a candidate producer's base subresource.\n" +
                    "pass_dump_manifest.tsv identifies full images, including presentation source target -4 and its crop.\n" +
                    "A blank preview column means the attachment is exported as exact raw bytes.\n" +
                    "Tile status encodes offset:bytes:x:y:width:height in surface_samples.raw,\n" +
                    "indexed by offset and hash in surface_samples.tsv. Metadata-only and skipped rows give their status.\n" +
                    "Compute-uniform rows record shader, descriptor binding (slot), NVN register (element), and effective descriptor size (guest_bytes).\n" +
                    "ubo:offset:bytes points to the prefix copied before dispatch, up to 4096 bytes. Match sampled bindings by their dispatch ordinal.\n" +
                    "temporal-shaders.json indexes translated shader bindings and uniform offsets without including shader bytecode.\n" +
                    "The last three prior runtime logs are retained across app launches.\n" +
                    "No game ROM, shaders, saves or user DLLs are included.\n" +
                    "Logs may contain device identifiers, filesystem paths, shader keys and GPU addresses.\n" +
                    "Review the report before sharing it.\n\n" + systemInfo + "\n")
                    .getBytes(StandardCharsets.UTF_8));
            byte[] buffer = new byte[64 * 1024]; // shared across all entries
            for (ArchiveInput input : inputs) {
                File file = input.file;
                String name = file.getName();
                boolean log = logName(name);
                FileInputStream source;
                try {
                    source = new FileInputStream(file);
                } catch (FileNotFoundException error) {
                    if (!log) throw error;
                    skipped.append(name).append(": log rotated before export\n");
                    continue;
                }
                try (FileInputStream stream = source) {
                    // Both parts of a log use the same opened file and one
                    // length snapshot, even if the pathname rotates or grows.
                    long length = stream.getChannel().size();
                    if (length <= 0) continue;
                    long count = log ? Math.min(length, LOG_TAIL) : length;
                    if (count > FILE_LIMIT || count > TOTAL_LIMIT - total) {
                        skipped.append(input.prefix).append(name).append(": exceeded export byte budget\n");
                        continue;
                    }
                    long copied = copy(zip, input.prefix + entryName(name), stream, length - count, count, buffer);
                    if (copied != count) {
                        if (!log) throw new IOException("File shortened while exporting: " + name);
                        skipped.append(name).append(": log shortened during export\n");
                    }
                    total += copied;
                    collected++;
                    if (log && length > count && LOG_HEAD <= TOTAL_LIMIT - total) {
                        long startup = Math.min(length, LOG_HEAD);
                        copied = copy(zip, "logs/" + name + ".startup.txt", stream, 0, startup, buffer);
                        if (copied != startup) skipped.append(name).append(": startup log shortened during export\n");
                        total += copied;
                    }
                }
            }
            entry(zip, "archive-status.txt", ("files=" + collected + "\nsource_bytes=" + total +
                    "\nsource_limit=" + TOTAL_LIMIT + "\n" +
                    (skipped.length() == 0 ? "skipped=none\n" : skipped.toString()))
                    .getBytes(StandardCharsets.UTF_8));
        }
    }
}
