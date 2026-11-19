"""Real standalone diagnostic ZIP writer: compile and execute the production Java class.

Requires only javac/java. Validates scoped artifact selection, manifest binding,
crash log retention, archive integrity and bounded log handling. It does not
make assumptions about source strings or invoke a game renderer.
"""
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
        Map<String, byte[]> result = new HashMap<>();
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
            "dump\tpresent\tpass\ttarget\n0\t1500\t18\t0\n"));
        put(root, "pass_00.ppm", bytes("new MRT 0"));
        put(root, "pass_00.raw", new byte[] {0, (byte)255, 31, 42});
        put(root, "pass_00.ppm.f32", new byte[] {1, 2, 3, 4});
        put(root, "pass_01.ppm", bytes("stale previous capture"));
        put(root, "shaders.dmsv", bytes("SECRET SHADERS"));
        put(root, "Lossless.dll", bytes("PRIVATE DLL"));
        put(root, "saves.dat", bytes("PRIVATE SAVE"));
        Files.createDirectory(root.resolve("RomFS"));
        put(root.resolve("RomFS"), "content.bin", bytes("PRIVATE GAME"));
        try {
            Files.createSymbolicLink(root.resolve("pass_02.ppm"), root.resolve("shaders.dmsv"));
        } catch (UnsupportedOperationException | SecurityException | IOException ignored) { }
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        DiagnosticArchive.write(root.toFile(), output, "driver=RADV Xclipse\n");
        Map<String, byte[]> zip = unpack(output.toByteArray());
        check(zip.containsKey("README.txt") && zip.containsKey("archive-status.txt"), "report metadata");
        check(zip.containsKey("metadata/capture_meta.txt"), "capture settings metadata");
        check(zip.containsKey("metadata/pass_dump_manifest.tsv"), "manifest");
        check(Arrays.equals(zip.get("captures/pass_00.raw"), new byte[]{0,(byte)255,31,42}), "exact GPU bytes");
        check(zip.containsKey("captures/pass_00.ppm.f32"), "float depth");
        check(!zip.containsKey("captures/pass_01.ppm"), "stale pass suppressed by manifest");
        check(!zip.containsKey("captures/pass_02.ppm"), "symlink excluded");
        for (String key : zip.keySet())
            check(!key.contains("shaders") && !key.contains("RomFS") &&
                  !key.contains("Lossless") && !key.contains("save"), "no game/private files");
        check(zip.containsKey("logs/log.prev.txt") && zip.containsKey("logs/log.prev.2.txt") &&
              zip.containsKey("logs/log.prev.3.txt"), "three prior runs");
        check(zip.get("logs/log.txt").length == DiagnosticArchive.LOG_TAIL, "tail bounded");
        check(zip.get("logs/log.txt")[0] == 'T', "tail retained");
        check(zip.get("logs/log.txt.startup.txt")[0] == 'H', "driver startup retained");
        check(new String(zip.get("README.txt"), StandardCharsets.UTF_8).contains(
            "High-overhead full MRT"), "report explains opt-in");

        // When a new one-shot capture is armed but its GPU fence has not
        // retired, never mislabel stale files as belonging to the new run.
        Files.delete(root.resolve("pass_dump_manifest.tsv"));
        ByteArrayOutputStream second = new ByteArrayOutputStream();
        DiagnosticArchive.write(root.toFile(), second, "driver=RADV\n");
        Map<String, byte[]> pending = unpack(second.toByteArray());
        check(!pending.containsKey("captures/pass_00.ppm"), "pending capture excludes stale target");
        boolean badFolder = false;
        try { DiagnosticArchive.write(root.resolve("RomFS/no-dir").toFile(),
                                     new ByteArrayOutputStream(), ""); }
        catch (IOException expected) { badFolder = true; }
        check(badFolder, "invalid folder rejected");
        System.out.println("DiagnosticArchive fixture PASS");
    }
}
"""

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
