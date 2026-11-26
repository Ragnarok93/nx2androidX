"""Real standalone diagnostic ZIP writer: compile and execute the production Java class.

Requires only javac/java. Validates scoped artifact selection, manifest binding,
crash log retention, archive integrity and bounded log handling. It does not
make assumptions about source strings or invoke a game renderer.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / "nxnative/dm/android/java/org/nx2a/DiagnosticArchive.java"

JAVA_TEST = r"""
package org.nx2a;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.zip.*;

public final class DiagnosticArchiveFixture {
    static void check(boolean ok, String why) {
        if (!ok) throw new AssertionError(why);
    }
    static byte[] bytes(String s) { return s.getBytes(StandardCharsets.UTF_8); }
    static Map<String, byte[]> unpack(byte[] data) throws Exception {
        Map<String, byte[]> result = new LinkedHashMap<>();
        try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(data))) {
            ZipEntry entry;
            byte[] b = new byte[4096];
            while ((entry = zip.getNextEntry()) != null) {
                ByteArrayOutputStream contents = new ByteArrayOutputStream();
                int n;
                while ((n = zip.read(b)) >= 0) if (n > 0) contents.write(b, 0, n);
                check(result.put(entry.getName(), contents.toByteArray()) == null, "duplicate entry");
                zip.closeEntry();
            }
        }
        return result;
    }
    static void put(Path root, String name, byte[] data) throws Exception {
        Files.write(root.resolve(name), data);
    }
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("nx2a-diag-fixture");
        check(DiagnosticArchive.allowedName("pass_00.ppm"), "preview allowlist");
        check(DiagnosticArchive.allowedName("pass_95.raw"), "raw allowlist");
        check(DiagnosticArchive.allowedName("pass_00.ppm.f32"), "float allowlist");
        check(!DiagnosticArchive.allowedName("pass_99.ppm/../saves"), "no traversal");
        check(!DiagnosticArchive.allowedName("shaders.dmsv"), "no shader bundle");
        put(root, "temporal-shaders.json", bytes("{}"));
        put(root, "shaders.dmsv", bytes("translated code is private"));
        check(!DiagnosticArchive.allowedName("env.txt"), "no external configuration");
        check(!DiagnosticArchive.allowedName("log.txt.bak"), "no extra files");
        check(!DiagnosticArchive.allowedName("pass_000000.ppm"), "no fake pass");
        byte[] big = new byte[(int)DiagnosticArchive.LOG_TAIL + 100];
        Arrays.fill(big, (byte)'T'); Arrays.fill(big, 0, 100, (byte)'H');
        put(root, "log.txt", big);
        put(root, "log.prev.txt", bytes("prior crash"));
        put(root, "log.prev.2.txt", bytes("older run"));
        put(root, "log.prev.3.txt", bytes("oldest retained run"));
        put(root, "capture_meta.txt", bytes("capture=player-armed\n"));
        put(root, "pass_dump_manifest.tsv", bytes(
            "dump\tpresent\tpass\ttarget\n0\t1500\t18\t0\n2\t1500\t18\t-1\n"));
        put(root, "gpu_draw_inputs.tsv", bytes("present\traw_hex\n1500\t02000000\n"));
        put(root, "gpu_draw_inputs.tsv.tmp", bytes("incomplete input report"));
        put(root, "surface_samples.raw", new byte[] {0, 2, 4, 6, 8});
        put(root, "surface_samples.tsv", bytes("offset\tbytes\tfnv1a64\n0\t5\t89\n"));
        put(root, "surface_usage.tsv", bytes("role\tstatus\ncolor\ttile:0:5:0:0:1:1\n"));
        put(root, "surface_usage.tsv.tmp", bytes("incomplete surface report"));
        put(root, "pass_00.ppm", bytes("new MRT 0"));
        put(root, "pass_00.raw", new byte[] {0, (byte)255, 31, 42});
        put(root, "pass_00.ppm.f32", new byte[] {1, 2, 3, 4});
        put(root, "pass_01.ppm", bytes("stale previous capture"));
        put(root, "pass_02.ppm.f32", new byte[] {5, 6, 7, 8});
        put(root, "pass_00.ppm.f32", bytes("stale sidecar from the same slot"));
        Files.setLastModifiedTime(root.resolve("pass_00.ppm.f32"),
            java.nio.file.attribute.FileTime.fromMillis(1000L));
        put(root, "shaders.dmsv", bytes("SECRET SHADERS"));
        put(root, "Lossless.dll", bytes("PRIVATE DLL"));
        put(root, "saves.dat", bytes("PRIVATE SAVE"));
        Files.createDirectory(root.resolve("RomFS"));
        put(root.resolve("RomFS"), "content.bin", bytes("PRIVATE GAME"));
        try {
            Files.createSymbolicLink(root.resolve("pass_02.ppm"), root.resolve("shaders.dmsv"));
        } catch (UnsupportedOperationException | SecurityException | IOException ignored) { }
        put(root, "capture_timeline.tsv", bytes("present\n1500\n1501\n"));
        put(root, "scaling_frames.tsv", bytes("ticket\tlatched_pct\n1500\t75\n"));
        Path completed = Files.createDirectory(root.resolve("diagnostic-capture-0"));
        put(completed, "complete", new byte[0]);
        put(completed, "pass_dump_manifest.tsv", bytes("dump\tpresent\n0\t1399\n"));
        put(completed, "pass_00.raw", bytes("first rolling frame"));
        put(completed, "pass_01.ppm", bytes("stale rolling frame"));
        put(completed, "saves.dat", bytes("PRIVATE SAVE"));
        Path pendingSlot = Files.createDirectory(root.resolve("diagnostic-capture-1"));
        put(pendingSlot, "pass_dump_manifest.tsv", bytes("dump\tpresent\n0\t1488\n"));
        put(pendingSlot, "pass_00.raw", bytes("pending GPU"));
        Path later = Files.createDirectory(root.resolve("diagnostic-capture-2"));
        put(later, "complete", new byte[0]);
        put(later, "surface_usage.tsv", bytes("completed without images"));
        put(later, "pass_00.raw", bytes("stale image in cleared slot"));
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        OutputStream mutableLogs = new FilterOutputStream(output) {
            private boolean rotated, truncated;
            @Override public void write(byte[] b, int off, int len) throws IOException {
                String entry = len < 30 ? new String(b, off, len, StandardCharsets.US_ASCII) : "";
                if (!rotated && entry.equals("logs/log.txt")) {
                    rotated = true;
                    Files.move(root.resolve("log.txt"), root.resolve("log.rotated"));
                    Files.write(root.resolve("log.txt"), bytes("replacement run"));
                } else if (!truncated && entry.equals("logs/log.prev.txt")) {
                    truncated = true;
                    Files.write(root.resolve("log.prev.txt"), new byte[0]);
                }
                out.write(b, off, len);
            }
        };
        DiagnosticArchive.write(root.toFile(), mutableLogs, "driver=RADV Xclipse\n");
        Map<String, byte[]> zip = unpack(output.toByteArray());
        check(zip.containsKey("README.txt") && zip.containsKey("archive-status.txt"), "report metadata");
        check(zip.containsKey("metadata/capture_meta.txt"), "capture settings metadata");
        check(zip.containsKey("metadata/capture_timeline.tsv"), "continuous timeline");
        check(Arrays.equals(zip.get("metadata/scaling_frames.tsv"),
            bytes("ticket\tlatched_pct\n1500\t75\n")), "scaling trace retained");
        check(Arrays.equals(zip.get("history/slot-0/captures/pass_00.raw"),
                            bytes("first rolling frame")), "completed frame");
        check(!zip.containsKey("history/slot-0/captures/pass_01.ppm"), "stale rolling image");
        check(!zip.containsKey("history/slot-0/captures/saves.dat"), "slot allowlist");
        check(!zip.containsKey("history/slot-1/metadata/pass_dump_manifest.tsv"), "unfinished slot");
        check(zip.containsKey("history/slot-2/metadata/surface_usage.tsv"), "later slot");
        check(!zip.containsKey("history/slot-2/captures/pass_00.raw"), "no stale raw without manifest");
        check(zip.containsKey("metadata/pass_dump_manifest.tsv"), "manifest");
        check(zip.containsKey("metadata/surface_usage.tsv"), "surface catalog");
        check(zip.containsKey("metadata/surface_samples.tsv"), "sample index");
        check(Arrays.equals(zip.get("metadata/temporal-shaders.json"), bytes("{}")), "temporal index");
        check(Arrays.equals(zip.get("captures/surface_samples.raw"), new byte[]{0,2,4,6,8}), "exact tile bytes");
        check(!zip.containsKey("metadata/surface_usage.tsv.tmp"), "partial catalog excluded");
        check(Arrays.equals(zip.get("metadata/gpu_draw_inputs.tsv"),
                            bytes("present\traw_hex\n1500\t02000000\n")), "completed GPU samples");
        check(!zip.containsKey("metadata/gpu_draw_inputs.tsv.tmp"), "temporary sample report excluded");
        check(Arrays.equals(zip.get("captures/pass_00.raw"), new byte[]{0,(byte)255,31,42}), "exact GPU bytes");
        check(zip.containsKey("captures/pass_02.ppm.f32"), "current float sidecar retained");
        check(!zip.containsKey("captures/pass_00.ppm.f32"), "stale sidecar suppressed on reused slot");
        List<String> names = new ArrayList<>(zip.keySet());
        check(names.indexOf("metadata/pass_dump_manifest.tsv") < names.indexOf("captures/pass_00.raw"),
              "manifest comes before the transfer bytes");
        check(names.indexOf("captures/pass_00.raw") < names.indexOf("captures/pass_00.ppm"),
              "exact readback comes before the preview");
        check(!zip.containsKey("captures/pass_01.ppm"), "stale pass suppressed by manifest");
        check(!zip.containsKey("captures/pass_02.ppm"), "symlink excluded");
        for (String key : zip.keySet())
            check(!key.contains("shaders.dmsv") && !key.contains("RomFS") &&
                  !key.contains("Lossless") && !key.contains("save"), "no game/private files");
        check(zip.containsKey("logs/log.prev.txt") && zip.containsKey("logs/log.prev.2.txt") &&
              zip.containsKey("logs/log.prev.3.txt"), "three prior runs");
        check(zip.get("logs/log.txt").length == DiagnosticArchive.LOG_TAIL, "tail bounded");
        check(zip.get("logs/log.txt")[0] == 'T', "tail retained");
        check(zip.get("logs/log.txt.startup.txt")[0] == 'H', "tail and driver startup use the same opened log after rotation");
        check(zip.get("logs/log.prev.txt").length == 0, "truncated log does not break the ZIP");
        check(new String(zip.get("archive-status.txt"), StandardCharsets.UTF_8)
            .contains("log.prev.txt: log shortened during export"), "truncated log is reported");
        check(new String(zip.get("README.txt"), StandardCharsets.UTF_8).contains(
            "GPU snapshots adds full attachment readbacks"), "report explains opt-in");

        // When a new one-shot capture is armed but its GPU fence has not
        // retired, never mislabel stale files as belonging to the new run.
        Files.setLastModifiedTime(root.resolve("pass_dump_manifest.tsv"),
            java.nio.file.attribute.FileTime.fromMillis(1000L));
        Files.setLastModifiedTime(root.resolve("surface_usage.tsv"),
            java.nio.file.attribute.FileTime.fromMillis(1000L));
        ByteArrayOutputStream second = new ByteArrayOutputStream();
        DiagnosticArchive.write(root.toFile(), second, "driver=RADV\n");
        Map<String, byte[]> pending = unpack(second.toByteArray());
        check(!pending.containsKey("captures/surface_samples.raw"), "orphaned tile bytes excluded");
        check(!pending.containsKey("metadata/surface_samples.tsv"), "orphaned sample index excluded");
        check(!pending.containsKey("metadata/pass_dump_manifest.tsv"), "pending capture excludes stale manifest");
        check(!pending.containsKey("captures/pass_00.ppm"), "pending capture excludes stale target");
        check(!pending.containsKey("metadata/gpu_draw_inputs.tsv"), "pending capture excludes stale samples");
        boolean badFolder = false;
        try { DiagnosticArchive.write(root.resolve("RomFS/no-dir").toFile(),
                                     new ByteArrayOutputStream(), ""); }
        catch (IOException expected) { badFolder = true; }
        check(badFolder, "invalid folder rejected");
        System.out.println("DiagnosticArchive fixture PASS");
    }
}
"""

class StandaloneDocumentRoutingTests(unittest.TestCase):
    def test_diagnostic_zip_cannot_be_routed_to_the_driver_importer(self):
        handlers = ("GameActivity", "DiagnosticsExport", "DriverDocuments", "SaveDocuments", "CompanionImporter")
        owners = {}
        for handler in handlers:
            source = ARCHIVE.with_name(handler + ".java").read_text(encoding="utf-8")
            for name, value in re.findall(
                    r"\bstatic final int (REQUEST\w*|EXPORT|IMPORT)\s*=\s*(0x[0-9a-fA-F]+)", source):
                code = int(value, 16)
                self.assertNotIn(code, owners, f"{handler}.{name} shares {value} with {owners.get(code)}")
                owners[code] = f"{handler}.{name}"
        self.assertIn("DiagnosticsExport.REQUEST", owners.values())
        self.assertIn("DriverDocuments.REQUEST", owners.values())


@unittest.skipUnless(shutil.which("javac") and shutil.which("java"), "JDK required")
class DiagnosticArchiveExecutableTests(unittest.TestCase):
    def test_java_archive_scope_integrity_and_memory_bounds(self):
        with tempfile.TemporaryDirectory(prefix="nx2a-diag-test-") as folder:
            root = Path(folder)
            source = root / "DiagnosticArchiveFixture.java"
            source.write_text(JAVA_TEST, encoding="utf-8")
            subprocess.run(["javac", "-source", "8", "-target", "8", "-d", str(root),
                            str(ARCHIVE), str(source)], check=True, timeout=30,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            run = subprocess.run(["java", "-ea", "-cp", str(root),
                                  "org.nx2a.DiagnosticArchiveFixture"],
                                 timeout=30, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PASS", run.stdout)

if __name__ == "__main__":
    unittest.main()
