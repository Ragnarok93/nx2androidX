# APK signing and update compatibility

## Stable signing identity

Local release builds and publishing workflows require the permanent signing certificate pinned in
[`packaging/android-apk/signing-cert.sha256`](../packaging/android-apk/signing-cert.sha256).
The application ID remains `com.nx.android`. Use a non-decreasing Android version code for updates.

The permanent signer was replaced on November 20, 2026 because no copy of the
previous private key was available. Existing installations with a different
certificate need a one-time migration before they can receive releases signed
with the new key. Once migrated, preserve this signing key for subsequent updates.
The builder APK and player-generated game APKs have independent signing identities.

Set these repository secrets under **Settings → Secrets and variables → Actions**:

- `NX2A_KEYSTORE_B64` holds the base64 encoding of the permanent keystore.
- `NX2A_KEYSTORE_PASSWORD` unlocks the keystore.
- `NX2A_KEY_PASSWORD` is optional when the private key has a different password.

GitHub Actions expects the alias `nx2android`. On a trusted machine with the permanent keystore and an
authenticated GitHub CLI, enter the two required secrets directly:

```bash
base64 < /path/to/original/nx2android.keystore | tr -d '\n' | gh secret set NX2A_KEYSTORE_B64 --repo Ragnarok93/nx2androidX
gh secret set NX2A_KEYSTORE_PASSWORD --repo Ragnarok93/nx2androidX
```

Then rerun the failed trusted branch build. CI checks the certificate before building and checks the final
APK before publishing. Both checks use the same certificate pin as Gradle. Trusted branch builds, manually
queued builds and tags require the repository signing secrets. A missing or mismatched key stops the build.
Pull requests run regression tests and native link checks without an APK artifact. For a local release, set `NX2A_KEYSTORE` to the permanent keystore path or place it at
`~/.android/nx2android.keystore`. The same password variables apply. Keep a private offline backup of the
keystore and password.

## Recovering installations signed by an ephemeral key

Alpha15 and the earlier CI-generated alpha16 artifacts were signed by temporary runner debug keystores (the signing secret was absent in those runs). The original private signing keys cannot be recovered from the installed APK, and signing a newer APK with a new key does not provide an ordinary Android update.

For a rooted Android device, a one-time **data-preserving migration** is possible:

1. Verify the new APK against the owner's expected digest and certificate.
2. Save the currently installed APK for rollback.
3. Stop the old process; preserve `/data/user/0/com.nx.android`, `/data/user_de/0/com.nx.android` and any per-package external data directories *outside* package-manager uninstall paths, preferably with same-volume moves rather than multi-gigabyte copies.
4. Uninstall the old signing identity without `pm uninstall -k`: that flag retains signature-bearing package settings and cannot reliably migrate to a new signer.
5. Install the permanently signed APK.
6. Restore data with the newly allocated app UID and correct SELinux labels; restore storage permissions as needed.
7. Preserve the original APK until the restored app and existing compilation workspace have been verified.

**WARNING:** This sequence requires root and reliable rollback/backup handling. Never simply uninstall or clear app data to fix a signature mismatch when work in `files/work` must be preserved. If the signed APK is obtained from an untrusted source or data cannot be backed up, stop before uninstalling.

Once migrated to the permanent key, normal `pm install -r` updates work as long as the same keystore signs every subsequent version.


## Reusing a prior build

For a normal builder upgrade, keep the existing app data. For a one-time signing
migration, preserve the builder's private `files/work` and the standalone signing
key before replacing the builder, then restore them under the new app's ownership.
Select the same game source and version to pick up `files/work/<TITLE_ID>_build`.
The porter reuses matching extractions, rewrites and shader bundles. Native object
receipts use compiler inputs and dependency contents, so a tools refresh with the
same file bytes does not force recompilation. Native objects from a builder that
predates these receipts are compiled once to establish the receipts.

A split APK export also checks the previous `-data` folder in its output location.
If the metadata and tracked files match, it keeps the original manifest so the
standalone app can reuse its imported copy. Keep the private workspace for
recompilation. The exported companion contains runtime files and cannot supply
all the inputs and receipts used by the compiler.

## Standalone game signing and installed-game upgrades

New titles derive independent, stable signing identities from a random private 32-byte seed and the canonical base title ID. An unrelated legacy key must never become the default signer for a new game. A prior APK with the same package takes precedence, and an existing legacy package receipt preserves its original identity. Older legacy titles without a package receipt require their original APK to identify the signer. Do not delete an existing installation to work around a signature error. When the original key or derivation seed is unavailable, an ordinary Android in-place update is impossible.

Keep a private offline backup of the standalone signing state (the signing-key JSON, seed-v1, titles directory and version receipts under the configured NX2A signing home). It is independent of the repository builder keystore. The Upgrade screen can restore a JSON key backup; the key is checked against the source APK certificate/public key and retained privately in a title-scoped recovery file. Never send private keys in diagnostics or commit them to the repository.

The Upgrade page discovers visible installed standalone games through launcher package queries and read-only ZIP metadata, including custom package names. It does not copy a whole installed APK or access another package's private files. For separate-data APKs, select the original matching companion folder if it cannot be found in Downloads. Upgrades write a separate APK in Download/nx2android, keep the original manifest and signer, and can open the Android installer for a user-confirmed in-place update. No uninstall, signature bypass, or automatic private-data access. Runtime ABI and guest rewrite compatibility checks still apply.

On Android, `Env` now migrates the complete signing-state family from `files/nx2a/signing-key*` to the persistent `files/standalone-signing-key*` location before a guest launch or compiler reset. If both locations already contain conflicting state, archive the entire older family under `files/standalone-signing-key.recovery/<id>/` without replacing either seed or key. The signer resolver checks existing private keys across those roots before attempting derivation. Recompilation also supplies installed-game APK paths to the porter so an installed signing identity takes precedence over an exported file. An inaccessible or unmatched installed signer stops the export instead of silently rotating the key.

Validation at this checkpoint: the old legacy-fallback policy failed a focused new-title selection regression, while the updated policy passed that regression and five additional policy tests (installed-source precedence, stale installed path, wrong package, archived state and unmatched signer). These used mocked cryptographic boundaries with the Git-identical porter/signing.py. The Git-identical StandaloneSigningState.kt compiled and ran on a local JVM, verifying complete-family migration, conflicting-state preservation, reset survival and idempotence. Full real-APK signing tests, Android compilation, discovery and installer behavior require CI and a device. No signing material was committed or exported.
