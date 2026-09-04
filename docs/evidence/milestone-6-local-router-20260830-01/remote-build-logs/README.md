# build04 remote static acceptance

Captured read-only from `ubuntu24-hushen` at `2026-08-30T04:56:58+00:00`.

Build tree:

`/data/openvela-contest-2026-gemini-s1-v1-build-20260830-04`

Source log tree:

`/data/openvela-contest-2026-gemini-s1-v1-milestone6-overlay-20260830-01`

## Result

- Build exit file: PASS (`0`).
- Final config gate: PASS. TFLite Micro, FlatBuffers, gemmlowp, kissfft, ruy, and the local router are enabled; `luncher_mini` remains disabled.
- Final artifacts: PASS. `.config`, `nuttx.elf`, stripped `nuttx`, and `vela.bin` all exist and have recorded byte sizes and SHA-256 values.
- Model embedding: PASS. The unique 52,008-byte model extracted directly from `vela.bin` matches the trained int8 artifact SHA-256 `8d9fa3a06fd9f0aef417cf92c72fad21a90efe74961077792f82e72ab6eb85d8`.
- TFLite Micro static integration: PASS. The final ELF contains a 0xCB28-byte model symbol, a 0x8000-byte tensor arena symbol, and linked FullyConnected/Softmax evaluation code. The compiled source registers only the int8 FullyConnected and Softmax operators.
- Router binary anchors: PASS. `vela.bin` contains the unique format string `[LOCAL AI %u%% %s] %s`, `TFL3`, `mooncat-demo-once`, and `CURRENT_EPOCH_PLUS_`.
- QuickApp listener static retention: PASS. The final ELF retains `quickapp_mq_listener_task.lto_priv.0`, its DWARF definition at source line 225, and the final binary contains the inbound/outbound queue and listener log anchors. An exact unmangled-name check would incorrectly miss this LTO-internalized symbol.
- Link report: PASS. The successful link reports SDRAM `14,112,308 B / 961,943,040 B` (`1.47%`) and reserve DRAM `36 B / 100 KB` (`0.04%`).

## Preserved failure history

retry3 was **not** the first build attempt. All three preceding failed logs and exit files are preserved verbatim beside the successful log:

1. `hushen-build.log` / `.exit`: FAIL (`exit 2`). The `.cc` source was selected but the build had no `CXXEXT = .cc`, producing an empty `CXX:` invocation and `arm-none-eabi-g++: fatal error: no input files`. Minimal fix: declare `.cc` as the C++ extension.
2. `hushen-build-retry1.log` / `.exit`: FAIL (`exit 2`). TFLM compilation reached `tensorflow/lite/kernels/internal/common.h` but could not find `fixedpoint/fixedpoint.h`. Minimal fix: add the existing offline TFLM, gemmlowp, kissfft, and ruy include roots plus the three required TFLM compile defines to the ai_agent C++ flags.
3. `hushen-build-retry2.log` / `.exit`: FAIL (`exit 2`). `agent_compat.h` was not C++ safe and failed with `invalid conversion from 'void*' to '_agent_task_args_t*'`. Minimal fix: remove that header from the C++ model translation unit and use its required success/failure values locally (`0` and `-1`).
4. `hushen-build-retry3.log` / `.exit`: PASS (`exit 0`) after the three minimal fixes above.

See `failure-history.txt` for hashes and exact failure anchors. No failed log was removed, rewritten, or presented as a successful attempt.

## LTO symbol boundary

The final ELF was linked with LTO. Its ordinary `nm` symbol table does **not** retain `mooncat_local_router_handle` or `mooncat_intent_model_predict`; therefore a strict final-ELF exported-symbol claim would be false.

The LTO input objects define both entries as global text (`T`) symbols, and the final ELF's DWARF contains their source-definition records at `mooncat_local_router.c:348` and `mooncat_intent_model.cc:102`. The linked ELF/flat binary also contains the model, arena, TFLM evaluation code, and router-specific strings. See `lto-symbol-audit.txt`.

## Evidence boundary

This directory proves a successful host build and static presence in build04. It does **not** prove Gemini S1 boot, board-side inference, inference latency, actual tensor-arena peak usage, native UI behavior, physical-button behavior, sensor behavior, or hardware stability. No packaging, flashing, PhoenixSuit, FEL/boot0, partition write, or device action was performed while collecting this evidence.
