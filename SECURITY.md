# Security policy

Do not post passwords, NVS dumps, personal books, or private logs in public issues. For a security vulnerability, use GitHub private vulnerability reporting if enabled; otherwise contact the maintainer before sharing exploit details publicly.

File Transfer is intended for a trusted local session. It uses a password-protected hotspot, session tokens for mutations, protected application paths, and validation of transfer data. HTTP traffic is not end-to-end encrypted; avoid using the reader as general sensitive-file storage. Exit transfer mode when finished. Clock credentials are provisioned at runtime and can be cleared in the browser manager.

There is no promise of security support for older releases. Update to current source and report the affected commit, reproduction steps, and impact without including real credentials.
