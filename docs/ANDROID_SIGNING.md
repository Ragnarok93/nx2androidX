# APK signing and update compatibility (alpha16 onwards)

## Stable signing identity

Release and manually dispatched Android APK builds must use the same persistent, privately held signing certificate. A random GitHub runner debug keystore cannot update the APK produced by a different runner. The workflow now refuses release/dispatch builds when either signing secret is absent, verifies the APK signature, and checks its certificate against the keystore.

For the owner's alpha16 migration, the permanent signing certificate's SHA-256 fingerprint is:

`24c2fde0b7e75d5a8325ad21079fccbc5cd0929fe26e0a3e6dda7b4235026953`

This is public certificate metadata **not** the private key. Keep the private keystore out of source control and GitHub Actions artifacts.

Set two repository secrets under **Settings > Secrets and variables > Actions**:

- `NX2A_KEYSTORE_B64`: the complete base64 encoding of the persistent PKCS12 keystore
- `NX2A_KEYSTORE_PASSWORD`: the corresponding keystore password

Keep the keystore and password backed up offline. Never regenerate the signing key for ordinary updates. The keystore alias must be `nx2android`; the key password defaults to the store password, consistent with `android/app/build.gradle.kts`. Future updateable APKs must preserve the application ID `com.nx.android`, the signer and a non-decreasing Android versionCode (increment for each release).

PR verification builds deliberately use a throwaway key even if the secrets are configured. Their artifact is labeled `nx2android-PR-EPHEMERAL-NOT-UPDATABLE` and must not be distributed as an update.

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
