from __future__ import annotations

from pathlib import Path
import hashlib
import re
import struct


PACK2 = Path("/data/openvela-contest-2026-gemini-s1-v1-pack-20260829-02")
PACK1 = Path("/data/openvela-contest-2026-gemini-s1-v1-pack-20260829-01")
LOGS = PACK2 / "logs"
NEW_IMAGE = PACK2 / (
    "output/Gemini-S1-MoonCat-AI-QuickApp-autostart-20260829-candidate-02.img"
)
OLD_IMAGE = PACK1 / "output/Gemini-S1-MoonCat-AI-contest-20260829-candidate-01.img"
BASE_IMAGE = PACK1 / (
    "lichee/out/r528s3/gemini-s1_nand/"
    "image-fps48-sparse-lcdstridefixed-20260815-r8/"
    "rtos_nuttx_r528s3-gemini-s1_uart0_128Mnand.img"
)
NEW_DUMP = PACK2 / (
    "audit/candidate/"
    "Gemini-S1-MoonCat-AI-QuickApp-autostart-20260829-candidate-02.img.dump"
)
OLD_DUMP = PACK1 / (
    "audit/candidate/Gemini-S1-MoonCat-AI-contest-20260829-candidate-01.img.dump"
)
BASE_DUMP = PACK1 / (
    "audit/baseline/rtos_nuttx_r528s3-gemini-s1_uart0_128Mnand.img.dump"
)
BUILD_ROOT = Path("/data/openvela-contest-2026-gemini-s1-v1-build-20260829-02")
VELA_BIN = BUILD_ROOT / "nuttx/vela.bin"
CONFIG = BUILD_ROOT / "nuttx/.config"
ELF = BUILD_ROOT / "nuttx/nuttx.elf"
RCS = BUILD_ROOT / (
    "vendor/allwinnertech/boards/r528/r528s3-gemini-s1/src/etc/init.d/rcS"
)


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


def checksum_u32_le(data: bytes) -> int:
    padded = data + b"\0" * ((-len(data)) % 4)
    words = struct.unpack("<" + "I" * (len(padded) // 4), padded)
    return sum(words) & 0xFFFFFFFF


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


def main() -> None:
    LOGS.mkdir(parents=True, exist_ok=True)
    expected_size, declared_count, items = parse_image_cfg(NEW_DUMP / "image.cfg")
    image_size = NEW_IMAGE.stat().st_size
    image_bytes = NEW_IMAGE.read_bytes()
    if expected_size != image_size:
        raise RuntimeError(
            f"image size mismatch: cfg={expected_size} actual={image_size}"
        )
    if declared_count != len(items):
        raise RuntimeError(
            f"item count mismatch: cfg={declared_count} parsed={len(items)}"
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
        padding_zero = all(byte == 0 for byte in image_bytes[actual_end:padded_end])
        extracted = (NEW_DUMP / name).read_bytes()
        payload_match = (
            len(extracted) == actual and extracted == image_bytes[offset:actual_end]
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
        payload = (NEW_DUMP / f"{group}.fex").read_bytes()
        vbytes = (NEW_DUMP / f"V{group}.fex").read_bytes()
        computed = checksum_u32_le(payload)
        stored = struct.unpack("<I", vbytes)[0] if len(vbytes) == 4 else -1
        passed = len(vbytes) == 4 and computed == stored
        v_pass &= passed
        vlines.append(
            f"{group}\t{len(payload)}\t{hashlib.sha256(payload).hexdigest()}\t"
            f"{len(vbytes)}\t{hashlib.sha256(vbytes).hexdigest()}\t"
            f"{computed}\t{stored}\t{'PASS' if passed else 'FAIL'}"
        )
    write_lines("vfile-independent-audit.tsv", vlines)

    old_diffs, old_size, old_hash = compare_payloads(
        items, "lit-candidate", OLD_DUMP, OLD_IMAGE
    )
    base_diffs, base_size, base_hash = compare_payloads(
        items, "rollback", BASE_DUMP, BASE_IMAGE
    )
    new_hash = sha256(NEW_IMAGE)

    nsh = (NEW_DUMP / "nsh.fex").read_bytes()
    vela = VELA_BIN.read_bytes()
    required_anchors = (
        b"hap://app/",
        b"vapp",
        b"ai_agent < /dev/null &",
        b"mooncat_coach_tick",
        b"mooncat-active-coach",
        b"MOONCAT ACTIVE COACH",
    )
    banned_anchors = (
        b"ILI9341_V2",
        b"BOOT_COLOR_SEQUENCE",
        b"PURE_COLOR_WRITE_SEQUENCE",
        b"SOFTWARE_SPI_GATE",
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

    required_config = (
        'CONFIG_BASE_DEFCONFIG="../vendor/allwinnertech/boards/r528/'
        'r528s3-gemini-s1/configs/nsh_minidisplay"',
        "CONFIG_FEATURE_FRAMEWORK=y",
        "CONFIG_QUICKAPP=y",
        "CONFIG_QUICKAPP_VAPP=y",
        "CONFIG_EXAMPLES_AI_AGENT_VELA=y",
        "CONFIG_FEATURE_SYSTEM_VELACLAW=y",
        "CONFIG_LV_NUTTX_LCD_CUSTOM_BUFFER=y",
        "CONFIG_LV_NUTTX_LCD_BUFFER_SIZE=120",
    )
    config_set = set(CONFIG.read_text(encoding="utf-8").splitlines())
    config_report = ["setting\tstatus"]
    config_pass = True
    for setting in required_config:
        passed = setting in config_set
        config_pass &= passed
        config_report.append(f"{setting}\t{'PASS' if passed else 'FAIL'}")
    write_lines("final-config-audit.tsv", config_report)

    rcs_text = RCS.read_text(encoding="utf-8")
    rcs_pass = (
        "openvela_ui &" in rcs_text
        and "#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA\n"
        "ai_agent < /dev/null &\n#endif" in rcs_text
    )
    vela_match = sha256(NEW_DUMP / "nsh.fex") == sha256(VELA_BIN)
    diff_pass = (
        old_diffs == ["nsh.fex", "Vnsh.fex"]
        and base_diffs == ["nsh.fex", "Vnsh.fex"]
    )

    summary = [
        f"candidate_path={NEW_IMAGE}",
        f"candidate_bytes={image_size}",
        f"candidate_sha256={new_hash}",
        f"lit_candidate_path={OLD_IMAGE}",
        f"lit_candidate_bytes={old_size}",
        f"lit_candidate_sha256={old_hash}",
        f"candidate_minus_lit_bytes={image_size - old_size}",
        f"rollback_path={BASE_IMAGE}",
        f"rollback_bytes={base_size}",
        f"rollback_sha256={base_hash}",
        f"candidate_minus_rollback_bytes={image_size - base_size}",
        f"declared_item_count={declared_count}",
        f"parsed_item_count={len(items)}",
        f"diff_vs_lit={' '.join(old_diffs)}",
        f"diff_vs_rollback={' '.join(base_diffs)}",
        f"nsh_bytes={(NEW_DUMP / 'nsh.fex').stat().st_size}",
        f"nsh_sha256={sha256(NEW_DUMP / 'nsh.fex')}",
        f"vela_bin_bytes={VELA_BIN.stat().st_size}",
        f"vela_bin_sha256={sha256(VELA_BIN)}",
        f"nsh_matches_vela_bin={'PASS' if vela_match else 'FAIL'}",
    ]
    write_lines("image-comparison-summary.txt", summary)

    artifacts = ["bytes\tsha256\tpath"]
    for path in (CONFIG, ELF, VELA_BIN, RCS, NEW_IMAGE):
        artifacts.append(f"{path.stat().st_size}\t{sha256(path)}\t{path}")
    write_lines("final-artifacts.sha256-stat.tsv", artifacts)

    gates = [
        f"imagewty_magic={'PASS' if image_bytes[:8] == b'IMAGEWTY' else 'FAIL'}",
        f"imagewty_files={'PASS' if declared_count == 22 and len(items) == 22 else 'FAIL'}",
        f"all_item_boundaries_padding_alignment={'PASS' if boundary_pass else 'FAIL'}",
        f"vfile_checks={'PASS' if v_pass else 'FAIL'}",
        f"payload_diff_only_nsh_and_Vnsh={'PASS' if diff_pass else 'FAIL'}",
        f"candidate_nsh_matches_vela_bin={'PASS' if vela_match else 'FAIL'}",
        f"quickapp_autostart_and_banned_string_scan={'PASS' if anchor_pass else 'FAIL'}",
        f"final_config={'PASS' if config_pass else 'FAIL'}",
        f"generated_rcS={'PASS' if rcs_pass else 'FAIL'}",
    ]
    write_lines("audit-gates.txt", gates)
    all_pass = all(
        (
            image_bytes[:8] == b"IMAGEWTY",
            declared_count == 22,
            len(items) == 22,
            boundary_pass,
            v_pass,
            diff_pass,
            vela_match,
            anchor_pass,
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
