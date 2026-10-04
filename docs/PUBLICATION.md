# Public export policy

This public repository uses fresh history. The private development repository remains separate.

Included: current source, generated font tables and licensed font sources, original test corpus, dependency notices, minimal vendor patch, build tools, and original/demo documentation images.

Excluded: personal books/covers, photographed commercial passages, Wi-Fi credentials, NVS/SD settings dumps, private device logs, old validation recordings, binaries, release ZIPs, local paths, and the original development history.

Seven Elecrow example files lack an established redistribution license in the reviewed upstream tree. They are not committed. The setup script fetches a specific upstream commit locally and verifies hashes; this is not a claim that those files are MIT. No binary distribution is provided by CI.

The README captures contain authored demo content. The browser image uses synthetic data. `tools/check-public.py` scans tracked files for prohibited paths/artifacts and recognizable secret/private-key formats; automated scanning cannot prove the absence of every possible secret. Keep credential provisioning at runtime.
