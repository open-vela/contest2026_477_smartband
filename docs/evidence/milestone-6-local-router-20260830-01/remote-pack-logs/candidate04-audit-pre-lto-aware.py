from __future__ import annotations

from array import array
from pathlib import Path
import hashlib
import re
import struct
import subprocess
import sys


PACK4 = Path("/data/openvela-contest-2026-gemini-s1-v1-pack-20260830-04")
PACK3 = Path("/data/openvela-contest-2026-gemini-s1-v1-pack-20260829-03")
PACK1 = Path("/data/openvela-contest-2026-gemini-s1-v1-pack-20260829-01")
BUILD4 = Path("/data/openvela-contest-2026-gemini-s1-v1-build-20260830-04")

CANDIDATE4_NAME = (
    "Gemini-S1-MoonCat-AI-Local-Router-20260830-candidate-04.img"
)
CANDIDATE3_NAME = (
    "Gemini-S1-MoonCat-AI-QuickApp-autostart-no-launcher-"
    "20260829-candidate-03.img"
)
ROLLBACK_RELATIVE = Path(
    "lichee/out/r528s3/gemini-s1_nand/"
    "image-fps48-sparse-lcdstridefixed-20260815-r8/"
    "rtos_nuttx_r528s3-gemini-s1_uart0_128Mnand.img"
)

LOGS = PACK4 / "logs"
NEW_IMAGE = PACK4 / "output" / CANDIDATE4_NAME
REFERENCE_IMAGE = PACK3 / "output" / CANDIDATE3_NAME
ROLLBACK_IMAGE = PACK1 / ROLLBACK_RELATIVE
NEW_DUMP = PACK4 / "audit/candidate" / f"{CANDIDATE4_NAME}.dump"
REFERENCE_DUMP = PACK3 / "audit/candidate" / f"{CANDIDATE3_NAME}.dump"
ROLLBACK_DUMP = PACK1 / (
    "audit/baseline/rtos_nuttx_r528s3-gemini-s1_uart0_128Mnand.img.dump"
)

VELA_BIN = BUILD4 / "nuttx/vela.bin"
CONFIG = BUILD4 / "nuttx/.config"
ELF = BUILD4 / "nuttx/nuttx.elf"
RCS = BUILD4 / (
    "vendor/allwinnertech/boards/r528/r528s3-gemini-s1/src/etc/init.d/rcS"
)
GENERATED_RCS = BUILD4 / "nuttx/arch/arm/src/board/etctmp/etc/init.d/rcS"
NM = BUILD4 / "prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-nm"
MODEL_SOURCE = BUILD4 / "packages/ai_agent/src/tools/mooncat_intent_model.cc"
MODEL_HEADER = BUILD4 / "packages/ai_agent/src/tools/mooncat_intent_model_data.h"

CANDIDATE3_BYTES = 71_837_696
CANDIDATE3_SHA256 = (
    "b5cc555a66247affe85399a6ffafa5e5c70b2ee442aa94da233a8dde1cd73500"
)
ROLLBACK_BYTES = 68_699_136
ROLLBACK_SHA256 = (
    "f42ac571713fd35fd04722de01dfc3e60af96d4c80daa8709e81a214436c2bbc"
)
EXPECTED_PAYLOAD_DIFFS = ["nsh.fex", "Vnsh.fex"]
EXPECTED_ITEMS = (
    "sys_config.fex",
    "config.fex",
    "split_xxxx.fex",
    "sys_partition.fex",
    "sunxi.fex",
    "boot0_nand.fex",
    "u-boot.fex",
    "toc1.fex",
    "toc0.fex",
    "fes1.fex",
    "boot_package.fex",
    "usbtool.fex",
    "aultools.fex",
    "aultls32.fex",
    "sunxi_mbr.fex",
    "dlinfo.fex",
    "nsh.fex",
    "Vnsh.fex",
    "res.fex",
    "Vres.fex",
    "usrdata.fex",
    "Vusrdata.fex",
)
MODEL_BYTES = 52_008
MODEL_SHA256 = "8d9fa3a06fd9f0aef417cf92c72fad21a90efe74961077792f82e72ab6eb85d8"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_image_cfg(path: Path) -> tuple[int, int, list[dict[str, int | str]]]:
    text = path.read_text(encoding="utf-8")
    total = re.search(r"^total_image_size=0x([0-9A-Fa-f]+);$", text, re.M)
    count = re.search(r"^number_of_files=0x([0-9A-Fa-f]+);$", text, re.M)
    if total is None or count is None:
        raise RuntimeError("missing IMAGEWTY metadata")
    pattern = re.compile(
        r"file_(\d+) \{.*?^filename=\"([^\"]+)\";"
        r".*?^stored_length=0x([0-9A-Fa-f]+);"
        r".*?^original_length=0x([0-9A-Fa-f]+);"
        r".*?^offset=0x([0-9A-Fa-f]+);.*?^\}",
        re.M | re.S,
    )
    items: list[dict[str, int | str]] = []
    for match in pattern.finditer(text):
        items.append(
            {
                "no": int(match.group(1)),
                "name": match.group(2),
                "stored": int(match.group(3), 16),
                "actual": int(match.group(4), 16),
                "offset": int(match.group(5), 16),
            }
        )
    return int(total.group(1), 16), int(count.group(1), 16), items


def file_region_matches(image: Path, offset: int, length: int, extracted: Path) -> bool:
    if extracted.stat().st_size != length:
        return False
    remaining = length
    with image.open("rb") as source, extracted.open("rb") as copy:
        source.seek(offset)
        while remaining:
            size = min(1024 * 1024, remaining)
            if source.read(size) != copy.read(size):
                return False
            remaining -= size
        return copy.read(1) == b""


def region_is_zero(image: Path, offset: int, length: int) -> bool:
    remaining = length
    with image.open("rb") as stream:
        stream.seek(offset)
        while remaining:
            chunk = stream.read(min(1024 * 1024, remaining))
            if not chunk or any(chunk):
                return False
            remaining -= len(chunk)
    return True


def checksum_u32_le_file(path: Path) -> int:
    total = 0
    carry = b""
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            chunk = carry + chunk
            aligned = len(chunk) & ~3
            words = array("I")
            words.frombytes(chunk[:aligned])
            if sys.byteorder != "little":
                words.byteswap()
            total = (total + sum(words)) & 0xFFFFFFFF
            carry = chunk[aligned:]
    if carry:
        total = (total + int.from_bytes(carry.ljust(4, b"\0"), "little")) & 0xFFFFFFFF
    return total


def dump_matches_image(image: Path, dump: Path) -> bool:
    expected_size, declared_count, items = parse_image_cfg(dump / "image.cfg")
    if expected_size != image.stat().st_size or declared_count != len(items):
        return False
    if [int(item["no"]) for item in items] != list(range(1, 23)):
        return False
    if tuple(str(item["name"]) for item in items) != EXPECTED_ITEMS:
        return False
    return all(
        file_region_matches(
            image,
            int(item["offset"]),
            int(item["actual"]),
            dump / str(item["name"]),
        )
        for item in items
    )


def model_bytes_from_header(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    body = text.split("g_mooncat_router_model[] = {", 1)[1].split("};", 1)[0]
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", body))


def write_lines(name: str, lines: list[str]) -> None:
    (LOGS / name).write_text("\n".join(lines) + "\n", encoding="utf-8")


def compare_payloads(
    items: list[dict[str, int | str]],
    label: str,
    reference_dump: Path,
    reference_image: Path,
) -> tuple[list[str], int, str]:
    lines = [
        "item\tstatus\treference_bytes\tcandidate_bytes\t"
        "reference_sha256\tcandidate_sha256"
    ]
    differences: list[str] = []
    for item in items:
        name = str(item["name"])
        reference = reference_dump / name
        candidate = NEW_DUMP / name
        reference_hash = sha256(reference)
        candidate_hash = sha256(candidate)
        same = (
            reference.stat().st_size == candidate.stat().st_size
            and reference_hash == candidate_hash
        )
        if not same:
            differences.append(name)
        lines.append(
            f"{name}\t{'SAME' if same else 'DIFF'}\t"
            f"{reference.stat().st_size}\t{candidate.stat().st_size}\t"
            f"{reference_hash}\t{candidate_hash}"
        )
    write_lines(f"payload-compare-vs-{label}.tsv", lines)
    return differences, reference_image.stat().st_size, sha256(reference_image)


def nm_defined_symbols(path: Path) -> dict[str, str]:
    completed = subprocess.run(
        [str(NM), "-a", "--defined-only", str(path)],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    symbols: dict[str, str] = {}
    for line in completed.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and re.fullmatch(r"[0-9A-Fa-f]+", fields[0]):
            symbols[fields[-1]] = fields[-2]
    return symbols


def nm_defined_sized_symbols(path: Path) -> list[tuple[str, int]]:
    completed = subprocess.run(
        [str(NM), "-a", "-S", "-C", "--defined-only", str(path)],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    symbols: list[tuple[str, int]] = []
    pattern = re.compile(
        r"^[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)\s+\S\s+(.+)$"
    )
    for line in completed.stdout.splitlines():
        match = pattern.match(line)
        if match:
            symbols.append((match.group(2), int(match.group(1), 16)))
    return symbols


def main() -> None:
    LOGS.mkdir(parents=True, exist_ok=True)
    expected_size, declared_count, items = parse_image_cfg(NEW_DUMP / "image.cfg")
    image_size = NEW_IMAGE.stat().st_size
    if expected_size != image_size:
        raise RuntimeError(
            f"image size mismatch: cfg={expected_size} actual={image_size}"
        )
    if declared_count != len(items):
        raise RuntimeError(
            f"item count mismatch: cfg={declared_count} parsed={len(items)}"
        )
    canonical_items = (
        [int(item["no"]) for item in items] == list(range(1, 23))
        and tuple(str(item["name"]) for item in items) == EXPECTED_ITEMS
    )

    boundary = [
        "no\titem\toffset_hex\tactual_bytes\tpadded_bytes\tpadding_bytes\t"
        "actual_end_hex\tpadded_end_hex\twithin_image\tnonoverlap\t"
        "offset_1k_aligned\tpadded_16_aligned\tpadding_zero\t"
        "payload_matches_extract"
    ]
    previous_end = 0
    boundary_pass = True
    for item in items:
        number = int(item["no"])
        name = str(item["name"])
        offset = int(item["offset"])
        actual = int(item["actual"])
        stored = int(item["stored"])
        actual_end = offset + actual
        padded_end = offset + stored
        within = actual_end <= image_size and padded_end <= image_size and stored >= actual
        nonoverlap = offset >= previous_end
        offset_aligned = offset % 1024 == 0
        stored_aligned = stored % 16 == 0
        padding_zero = region_is_zero(NEW_IMAGE, actual_end, stored - actual)
        payload_match = file_region_matches(
            NEW_IMAGE, offset, actual, NEW_DUMP / name
        )
        checks = (
            within,
            nonoverlap,
            offset_aligned,
            stored_aligned,
            padding_zero,
            payload_match,
        )
        boundary_pass &= all(checks)
        boundary.append(
            f"{number}\t{name}\t0x{offset:X}\t{actual}\t{stored}\t"
            f"{stored - actual}\t0x{actual_end:X}\t0x{padded_end:X}\t"
            + "\t".join("PASS" if result else "FAIL" for result in checks)
        )
        previous_end = padded_end
    write_lines("item-boundary-audit.tsv", boundary)

    vlines = [
        "group\tpayload_bytes\tpayload_sha256\tV_bytes\tV_sha256\t"
        "computed_u32\tstored_u32\tstatus"
    ]
    v_pass = True
    for group in ("nsh", "res", "usrdata"):
        payload = NEW_DUMP / f"{group}.fex"
        vbytes = (NEW_DUMP / f"V{group}.fex").read_bytes()
        computed = checksum_u32_le_file(payload)
        stored = struct.unpack("<I", vbytes)[0] if len(vbytes) == 4 else -1
        passed = len(vbytes) == 4 and computed == stored
        v_pass &= passed
        vlines.append(
            f"{group}\t{payload.stat().st_size}\t{sha256(payload)}\t"
            f"{len(vbytes)}\t{hashlib.sha256(vbytes).hexdigest()}\t"
            f"{computed}\t{stored}\t{'PASS' if passed else 'FAIL'}"
        )
    write_lines("vfile-independent-audit.tsv", vlines)

    reference_diffs, reference_size, reference_hash = compare_payloads(
        items, "candidate-03", REFERENCE_DUMP, REFERENCE_IMAGE
    )
    rollback_diffs, rollback_size, rollback_hash = compare_payloads(
        items, "rollback", ROLLBACK_DUMP, ROLLBACK_IMAGE
    )
    new_hash = sha256(NEW_IMAGE)
    reference_lock_pass = (
        reference_size == CANDIDATE3_BYTES
        and reference_hash == CANDIDATE3_SHA256
    )
    rollback_lock_pass = (
        rollback_size == ROLLBACK_BYTES and rollback_hash == ROLLBACK_SHA256
    )
    candidate_dump_binding_pass = dump_matches_image(NEW_IMAGE, NEW_DUMP)
    reference_dump_binding_pass = dump_matches_image(REFERENCE_IMAGE, REFERENCE_DUMP)
    rollback_dump_binding_pass = dump_matches_image(ROLLBACK_IMAGE, ROLLBACK_DUMP)

    nsh = (NEW_DUMP / "nsh.fex").read_bytes()
    vela = VELA_BIN.read_bytes()
    required_anchors = (
        b"hap://app/",
        b"vapp",
        b"ai_agent < /dev/null &",
        b"mooncat_coach_tick",
        b"mooncat-active-coach",
        b"MOONCAT ACTIVE COACH",
        b"[LOCAL AI",
        b"TFL3",
    )
    banned_anchors = (
        b"ILI9341_V2",
        b"BOOT_COLOR_SEQUENCE",
        b"PURE_COLOR_WRITE_SEQUENCE",
        b"SOFTWARE_SPI_GATE",
        b"luncher_mini_main",
        b"luncher_mini &",
    )
    anchor_lines = ["kind\tstring\tnsh_count\tvela_count\tstatus"]
    anchor_pass = True
    for value in required_anchors:
        nsh_count = nsh.count(value)
        vela_count = vela.count(value)
        passed = nsh_count > 0 and vela_count > 0
        anchor_pass &= passed
        anchor_lines.append(
            f"REQUIRED\t{value.decode()}\t{nsh_count}\t{vela_count}\t"
            f"{'PASS' if passed else 'FAIL'}"
        )
    for value in banned_anchors:
        nsh_count = nsh.count(value)
        vela_count = vela.count(value)
        passed = nsh_count == 0 and vela_count == 0
        anchor_pass &= passed
        anchor_lines.append(
            f"BANNED\t{value.decode()}\t{nsh_count}\t{vela_count}\t"
            f"{'PASS' if passed else 'FAIL'}"
        )
    write_lines("binary-anchor-audit.tsv", anchor_lines)

    symbols = nm_defined_symbols(ELF)
    required_symbols = (
        "vapp_main",
        "ai_agent_main",
        "quickapp_mq_listener_task",
        "tool_mooncat_coach_execute",
        "openvela_ui_main",
        "mooncat_local_router_handle",
        "mooncat_intent_model_predict",
    )
    banned_symbols = ("luncher_mini_main",)
    symbol_lines = ["kind\tsymbol\ttype\tstatus"]
    symbol_pass = True
    for name in required_symbols:
        present = name in symbols
        symbol_pass &= present
        symbol_lines.append(
            f"REQUIRED\t{name}\t{symbols.get(name, '-')}\t"
            f"{'PRESENT' if present else 'MISSING'}"
        )
    for name in banned_symbols:
        absent = name not in symbols
        symbol_pass &= absent
        symbol_lines.append(
            f"BANNED\t{name}\t{symbols.get(name, '-')}\t"
            f"{'ABSENT' if absent else 'PRESENT'}"
        )
    write_lines("elf-defined-symbol-audit.tsv", symbol_lines)

    embedded_model = model_bytes_from_header(MODEL_HEADER)
    model_source = MODEL_SOURCE.read_text(encoding="utf-8")
    sized_symbols = nm_defined_sized_symbols(ELF)
    arena_sizes = [
        size for name, size in sized_symbols if name.endswith("g_tensor_arena")
    ]
    model_sizes = [
        size for name, size in sized_symbols if name.endswith("g_mooncat_router_model")
    ]
    model_checks = (
        ("model_bytes_52008", len(embedded_model) == MODEL_BYTES),
        ("model_sha256_locked", hashlib.sha256(embedded_model).hexdigest() == MODEL_SHA256),
        ("model_tfl3_identifier", embedded_model[4:8] == b"TFL3"),
        ("exact_model_once_in_vela_bin", vela.count(embedded_model) == 1),
        ("elf_model_symbol_size_52008", model_sizes == [MODEL_BYTES]),
        ("resolver_capacity_2", "MicroMutableOpResolver<2>" in model_source),
        (
            "fully_connected_int8_registered",
            "AddFullyConnected(tflite::Register_FULLY_CONNECTED_INT8())" in model_source,
        ),
        (
            "softmax_int8_registered",
            "AddSoftmax(tflite::Register_SOFTMAX_INT8())" in model_source,
        ),
        ("tensor_arena_32k", "kTensorArenaBytes = 32 * 1024" in model_source),
        ("tensor_arena_used", "g_tensor_arena[kTensorArenaBytes]" in model_source),
        ("elf_tensor_arena_symbol_size_32768", arena_sizes == [32 * 1024]),
    )
    model_static_pass = all(result for _, result in model_checks)
    model_report = ["gate\tstatus"]
    model_report.extend(
        f"{name}\t{'PASS' if result else 'FAIL'}" for name, result in model_checks
    )
    model_report.extend(
        (
            f"embedded_model_bytes\t{len(embedded_model)}",
            f"embedded_model_sha256\t{hashlib.sha256(embedded_model).hexdigest()}",
            f"vela_exact_model_occurrences\t{vela.count(embedded_model)}",
            f"elf_model_symbol_sizes\t{' '.join(str(size) for size in model_sizes)}",
            f"elf_tensor_arena_symbol_sizes\t{' '.join(str(size) for size in arena_sizes)}",
            "evidence_boundary\tSTATIC_ONLY_NOT_DEVICE_LATENCY_OR_RUNTIME_PEAK",
        )
    )
    write_lines("model-static-audit.tsv", model_report)

    required_config = (
        'CONFIG_BASE_DEFCONFIG="../vendor/allwinnertech/boards/r528/'
        'r528s3-gemini-s1/configs/nsh_minidisplay"',
        "CONFIG_FEATURE_FRAMEWORK=y",
        "CONFIG_QUICKAPP=y",
        "CONFIG_QUICKAPP_VAPP=y",
        "# CONFIG_LUNCHER_MINI_APP is not set",
        "CONFIG_EXAMPLES_AI_AGENT_VELA=y",
        "CONFIG_FEATURE_SYSTEM_VELACLAW=y",
        "CONFIG_LV_NUTTX_LCD_CUSTOM_BUFFER=y",
        "CONFIG_LV_NUTTX_LCD_BUFFER_SIZE=120",
        "CONFIG_SYSTEM_FLATBUFFERS=y",
        "CONFIG_MATH_GEMMLOWP=y",
        "CONFIG_MATH_KISSFFT=y",
        "CONFIG_MATH_RUY=y",
        "CONFIG_TFLITEMICRO=y",
        "CONFIG_AI_AGENT_LOCAL_TOOL_ROUTER=y",
    )
    config_set = set(CONFIG.read_text(encoding="utf-8").splitlines())
    config_report = ["setting\tstatus"]
    config_pass = True
    for setting in required_config:
        passed = setting in config_set
        config_pass &= passed
        config_report.append(f"{setting}\t{'PASS' if passed else 'FAIL'}")
    launcher_enabled = "CONFIG_LUNCHER_MINI_APP=y" in config_set
    config_pass &= not launcher_enabled
    config_report.append(
        "CONFIG_LUNCHER_MINI_APP=y\t"
        + ("FAIL" if launcher_enabled else "ABSENT")
    )
    write_lines("final-config-audit.tsv", config_report)

    rcs_text = RCS.read_text(encoding="utf-8")
    generated_rcs_text = GENERATED_RCS.read_text(encoding="utf-8")
    rcs_pass = (
        "openvela_ui &" in rcs_text
        and "#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA\n"
        "ai_agent < /dev/null &\n#endif" in rcs_text
        and "openvela_ui &" in generated_rcs_text
        and "ai_agent < /dev/null &" in generated_rcs_text
        and "luncher_mini" not in generated_rcs_text
    )
    vela_match = sha256(NEW_DUMP / "nsh.fex") == sha256(VELA_BIN)
    diff_pass = (
        reference_diffs == EXPECTED_PAYLOAD_DIFFS
        and rollback_diffs == EXPECTED_PAYLOAD_DIFFS
    )

    summary = [
        f"candidate_path={NEW_IMAGE}",
        f"candidate_bytes={image_size}",
        f"candidate_sha256={new_hash}",
        f"candidate03_path={REFERENCE_IMAGE}",
        f"candidate03_bytes={reference_size}",
        f"candidate03_sha256={reference_hash}",
        f"candidate_minus_candidate03_bytes={image_size - reference_size}",
        f"rollback_path={ROLLBACK_IMAGE}",
        f"rollback_bytes={rollback_size}",
        f"rollback_sha256={rollback_hash}",
        f"candidate_minus_rollback_bytes={image_size - rollback_size}",
        f"declared_item_count={declared_count}",
        f"parsed_item_count={len(items)}",
        f"diff_vs_candidate03={' '.join(reference_diffs)}",
        f"diff_vs_rollback={' '.join(rollback_diffs)}",
        f"nsh_bytes={(NEW_DUMP / 'nsh.fex').stat().st_size}",
        f"nsh_sha256={sha256(NEW_DUMP / 'nsh.fex')}",
        f"vela_bin_bytes={VELA_BIN.stat().st_size}",
        f"vela_bin_sha256={sha256(VELA_BIN)}",
        f"nsh_matches_vela_bin={'PASS' if vela_match else 'FAIL'}",
    ]
    write_lines("image-comparison-summary.txt", summary)

    artifacts = ["bytes\tsha256\tpath"]
    for path in (CONFIG, ELF, VELA_BIN, RCS, GENERATED_RCS, NEW_IMAGE):
        artifacts.append(f"{path.stat().st_size}\t{sha256(path)}\t{path}")
    write_lines("final-artifacts.sha256-stat.tsv", artifacts)

    gates = [
        f"imagewty_magic={'PASS' if NEW_IMAGE.open('rb').read(8) == b'IMAGEWTY' else 'FAIL'}",
        f"imagewty_files={'PASS' if declared_count == 22 and len(items) == 22 and canonical_items else 'FAIL'}",
        f"all_item_boundaries_padding_alignment={'PASS' if boundary_pass else 'FAIL'}",
        f"vfile_checks={'PASS' if v_pass else 'FAIL'}",
        f"candidate03_reference_lock={'PASS' if reference_lock_pass else 'FAIL'}",
        f"rollback_reference_lock={'PASS' if rollback_lock_pass else 'FAIL'}",
        f"candidate_dump_bound_to_image={'PASS' if candidate_dump_binding_pass else 'FAIL'}",
        f"candidate03_dump_bound_to_image={'PASS' if reference_dump_binding_pass else 'FAIL'}",
        f"rollback_dump_bound_to_image={'PASS' if rollback_dump_binding_pass else 'FAIL'}",
        f"payload_diff_only_nsh_and_Vnsh={'PASS' if diff_pass else 'FAIL'}",
        f"candidate_nsh_matches_vela_bin={'PASS' if vela_match else 'FAIL'}",
        f"binary_anchor_and_banned_string_scan={'PASS' if anchor_pass else 'FAIL'}",
        f"elf_defined_symbol_scan={'PASS' if symbol_pass else 'FAIL'}",
        f"model_operators_and_32k_arena_static={'PASS' if model_static_pass else 'FAIL'}",
        f"final_config={'PASS' if config_pass else 'FAIL'}",
        f"generated_rcS={'PASS' if rcs_pass else 'FAIL'}",
    ]
    write_lines("audit-gates.txt", gates)
    all_pass = all(
        (
            NEW_IMAGE.open("rb").read(8) == b"IMAGEWTY",
            declared_count == 22,
            len(items) == 22,
            canonical_items,
            boundary_pass,
            v_pass,
            reference_lock_pass,
            rollback_lock_pass,
            candidate_dump_binding_pass,
            reference_dump_binding_pass,
            rollback_dump_binding_pass,
            diff_pass,
            vela_match,
            anchor_pass,
            symbol_pass,
            model_static_pass,
            config_pass,
            rcs_pass,
        )
    )
    print("\n".join(gates))
    print("\n".join(summary[:12]))
    if not all_pass:
        raise SystemExit("one or more audit gates failed")


if __name__ == "__main__":
    main()
