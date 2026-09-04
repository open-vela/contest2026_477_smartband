# Milestone 6 remote pack04 evidence

This directory mirrors all 64 files from the remote pack log directory plus one stat/hash record for the final candidate. The 71.9 MB IMG itself is intentionally not copied into the repository.

## Final candidate

- Remote pack: `/data/openvela-contest-2026-gemini-s1-v1-pack-20260830-04`
- Remote image: `/data/openvela-contest-2026-gemini-s1-v1-pack-20260830-04/output/Gemini-S1-MoonCat-AI-Local-Router-20260830-candidate-04.img`
- Bytes: `71,915,520`
- SHA-256: `da8f82def499f297a5369c04dd4b6f040d70f340935b510883b4e5f1f9c92d6b`
- Final static audit: `16/16 PASS`, exit `0`

The final IMAGEWTY container has 22 canonical items. Boundary, padding, alignment, extracted-payload readback, `Vnsh`/`Vres`/`Vusrdata`, candidate03 and rollback locks, all three dump-to-image bindings, final config, generated `rcS`, binary anchors, LTO-aware function evidence, the exact 52,008-byte model, its two registered int8 operators, and the 32 KiB tensor arena all passed. Relative to both candidate03 and the locked rollback, only `nsh.fex` and `Vnsh.fex` differ.

The authoritative summaries are [audit-gates.txt](audit-gates.txt), [candidate04-audit-attempt2.stdout](candidate04-audit-attempt2.stdout), [image-comparison-summary.txt](image-comparison-summary.txt), [model-static-audit.tsv](model-static-audit.tsv), and [candidate04-final.sha256-stat.tsv](candidate04-final.sha256-stat.tsv).

## Preserved execution history

1. The official `lichee/tools/scripts/pack_img.sh` was run with the same pack03 arguments. It produced the expected inputs but exited `1` when its direct dragon call could not find the host system `/lib/ld-linux.so.2`; see [pack_img.log](pack_img.log) and [pack_img.exit](pack_img.exit).
2. The same official dragon binary was then run through the existing task-private i386 loader/runtime, without downloading anything. It exited `0`; see [dragon-final.command](dragon-final.command), [dragon-final.log](dragon-final.log), and [dragon-final.exit](dragon-final.exit).
3. IMAGEWTY info and extraction exited `0`, reported 22 files, and independently accepted all three V-file checksums.
4. Audit attempt 1 exited `1` because its original `nm` gate was not LTO-aware. That real failure is retained in [candidate04-audit.stdout](candidate04-audit.stdout), [candidate04-audit.stderr](candidate04-audit.stderr), and [candidate04-audit.exit](candidate04-audit.exit). The historical `candidate04-audit.sha256`/`candidate04-audit-pre-lto-aware.sha256` value is the attempt-1 script hash.
5. The corrected LTO/DWARF-aware audit exited `0` with all 16 gates passing. Its authoritative script hash is [candidate04-audit-lto-aware.sha256](candidate04-audit-lto-aware.sha256); the command, stdout, stderr, and exit are retained with the `attempt2` prefix. The mirrored `candidate04-audit.py` matches this final hash.

A lowercase-name image created before naming was normalized remains on the remote host and has exactly the same bytes and SHA-256 as the uppercase `Local-Router` candidate. It was not copied into the repository.

## Evidence boundary

This milestone proves model training/host behavior elsewhere in the milestone evidence and proves firmware build/package/static image properties here. It does **not** prove Gemini S1 boot, on-device inference latency, tensor-arena runtime peak, native UI behavior, or QuickApp physical-button interaction; those remain `NOT_TESTED`.

PhoenixSuit, FEL, boot0 mode, partition writes, and flashing were `NOT_PERFORMED`.
