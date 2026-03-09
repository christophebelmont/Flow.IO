"""
Generate compile-time default macros with explicit ownership.

Owners:
- module: intrinsic module behavior default (never overridden by Wokwi JSON)
- profile: board/profile wiring default (can be overridden by Wokwi JSON)

Input JSON:
  wokwi/default_overrides.json

Output header:
  src/Core/Generated/WokwiDefaultOverrides_Generated.h
"""

import json
import os
from pathlib import Path
from typing import Dict, List, Tuple

env = None
try:
    Import("env")  # type: ignore[name-defined]
except Exception:
    pass


OUTPUT_REL = Path("src/Core/Generated/WokwiDefaultOverrides_Generated.h")
DEFAULT_INPUT_REL = Path("wokwi/default_overrides.json")

# key, macro, owner, value_type, default_cpp
SPECS: List[Tuple[str, str, str, str, str]] = [
    # MQTT (profile wiring)
    ("mq_en", "FLOW_WIRDEF_MQ_EN", "profile", "bool", "true"),
    ("mq_host", "FLOW_WIRDEF_MQ_HOST", "profile", "string", "\"flowio.cloud.shiftr.io\""),
    ("mq_port", "FLOW_WIRDEF_MQ_PORT", "profile", "int32", "Limits::Mqtt::Defaults::Port"),
    ("mq_user", "FLOW_WIRDEF_MQ_USER", "profile", "string", "\"flowio\""),
    ("mq_pass", "FLOW_WIRDEF_MQ_PASS", "profile", "string", "\"LNqGl1OPt4RhFuNE\""),
    ("mq_base", "FLOW_WIRDEF_MQ_BASE", "profile", "string", "\"flowio\""),
    ("mq_tid", "FLOW_WIRDEF_MQ_TID", "profile", "string", "\"\""),

    # IO module behavior
    ("io_ads", "FLOW_MODDEF_IO_ADS", "module", "int32", "125"),
    ("io_ds", "FLOW_MODDEF_IO_DS", "module", "int32", "2000"),
    ("io_din", "FLOW_MODDEF_IO_DIN", "module", "int32", "100"),
    ("io_agai", "FLOW_MODDEF_IO_AGAI", "module", "int32", "ADS1X15_GAIN_6144MV"),
    ("io_arat", "FLOW_MODDEF_IO_ARAT", "module", "int32", "1"),
    ("io_tren", "FLOW_MODDEF_IO_TREN", "module", "bool", "true"),
    ("io_trms", "FLOW_MODDEF_IO_TRMS", "module", "int32", "((int32_t)Limits::IoTracePeriodMs)"),

    # IO profile wiring
    ("io_en", "FLOW_WIRDEF_IO_EN", "profile", "bool", "true"),
    ("io_sda", "FLOW_WIRDEF_IO_SDA", "profile", "int32", "21"),
    ("io_scl", "FLOW_WIRDEF_IO_SCL", "profile", "int32", "22"),
    ("io_aiad", "FLOW_WIRDEF_IO_AIAD", "profile", "uint8", "0x48u"),
    ("io_aead", "FLOW_WIRDEF_IO_AEAD", "profile", "uint8", "0x49u"),
    ("io_pcfen", "FLOW_WIRDEF_IO_PCFEN", "profile", "bool", "true"),
    ("io_pcfad", "FLOW_WIRDEF_IO_PCFAD", "profile", "uint8", "0x20u"),
    ("io_pcfmk", "FLOW_WIRDEF_IO_PCFMK", "profile", "uint8", "0u"),
    ("io_pcfal", "FLOW_WIRDEF_IO_PCFAL", "profile", "bool", "true"),

    # IO analog wiring defaults (a0..a5)
    ("io_a0s", "FLOW_WIRDEF_IO_A0S", "profile", "uint8", "IO_SRC_ADS_INTERNAL_SINGLE"),
    ("io_a0c", "FLOW_WIRDEF_IO_A0C", "profile", "uint8", "0u"),
    ("io_a00", "FLOW_WIRDEF_IO_A00", "profile", "float", "((FLOW_WIRDEF_IO_A0S == IO_SRC_ADS_EXTERNAL_DIFF) ? Calib::Orp::ExternalC0 : Calib::Orp::InternalC0)"),
    ("io_a01", "FLOW_WIRDEF_IO_A01", "profile", "float", "((FLOW_WIRDEF_IO_A0S == IO_SRC_ADS_EXTERNAL_DIFF) ? Calib::Orp::ExternalC1 : Calib::Orp::InternalC1)"),
    ("io_a0p", "FLOW_WIRDEF_IO_A0P", "profile", "int32", "0"),
    ("io_a0n", "FLOW_WIRDEF_IO_A0N", "profile", "float", "-32768.0f"),
    ("io_a0x", "FLOW_WIRDEF_IO_A0X", "profile", "float", "32767.0f"),

    ("io_a1s", "FLOW_WIRDEF_IO_A1S", "profile", "uint8", "IO_SRC_ADS_INTERNAL_SINGLE"),
    ("io_a1c", "FLOW_WIRDEF_IO_A1C", "profile", "uint8", "1u"),
    ("io_a10", "FLOW_WIRDEF_IO_A10", "profile", "float", "((FLOW_WIRDEF_IO_A1S == IO_SRC_ADS_EXTERNAL_DIFF) ? Calib::Ph::ExternalC0 : Calib::Ph::InternalC0)"),
    ("io_a11", "FLOW_WIRDEF_IO_A11", "profile", "float", "((FLOW_WIRDEF_IO_A1S == IO_SRC_ADS_EXTERNAL_DIFF) ? Calib::Ph::ExternalC1 : Calib::Ph::InternalC1)"),
    ("io_a1p", "FLOW_WIRDEF_IO_A1P", "profile", "int32", "1"),
    ("io_a1n", "FLOW_WIRDEF_IO_A1N", "profile", "float", "-32768.0f"),
    ("io_a1x", "FLOW_WIRDEF_IO_A1X", "profile", "float", "32767.0f"),

    ("io_a2s", "FLOW_WIRDEF_IO_A2S", "profile", "uint8", "IO_SRC_ADS_INTERNAL_SINGLE"),
    ("io_a2c", "FLOW_WIRDEF_IO_A2C", "profile", "uint8", "2u"),
    ("io_a20", "FLOW_WIRDEF_IO_A20", "profile", "float", "Calib::Psi::DefaultC0"),
    ("io_a21", "FLOW_WIRDEF_IO_A21", "profile", "float", "Calib::Psi::DefaultC1"),
    ("io_a2p", "FLOW_WIRDEF_IO_A2P", "profile", "int32", "1"),
    ("io_a2n", "FLOW_WIRDEF_IO_A2N", "profile", "float", "-32768.0f"),
    ("io_a2x", "FLOW_WIRDEF_IO_A2X", "profile", "float", "32767.0f"),

    ("io_a3s", "FLOW_WIRDEF_IO_A3S", "profile", "uint8", "IO_SRC_ADS_INTERNAL_SINGLE"),
    ("io_a3c", "FLOW_WIRDEF_IO_A3C", "profile", "uint8", "3u"),
    ("io_a30", "FLOW_WIRDEF_IO_A30", "profile", "float", "1.0f"),
    ("io_a31", "FLOW_WIRDEF_IO_A31", "profile", "float", "0.0f"),
    ("io_a3p", "FLOW_WIRDEF_IO_A3P", "profile", "int32", "3"),
    ("io_a3n", "FLOW_WIRDEF_IO_A3N", "profile", "float", "-32768.0f"),
    ("io_a3x", "FLOW_WIRDEF_IO_A3X", "profile", "float", "32767.0f"),

    ("io_a4s", "FLOW_WIRDEF_IO_A4S", "profile", "uint8", "IO_SRC_DS18_WATER"),
    ("io_a4c", "FLOW_WIRDEF_IO_A4C", "profile", "uint8", "0u"),
    ("io_a40", "FLOW_WIRDEF_IO_A40", "profile", "float", "1.0f"),
    ("io_a41", "FLOW_WIRDEF_IO_A41", "profile", "float", "0.0f"),
    ("io_a4p", "FLOW_WIRDEF_IO_A4P", "profile", "int32", "1"),
    ("io_a4n", "FLOW_WIRDEF_IO_A4N", "profile", "float", "Calib::Temperature::Ds18MinValidC"),
    ("io_a4x", "FLOW_WIRDEF_IO_A4X", "profile", "float", "Calib::Temperature::Ds18MaxValidC"),

    ("io_a5s", "FLOW_WIRDEF_IO_A5S", "profile", "uint8", "IO_SRC_DS18_AIR"),
    ("io_a5c", "FLOW_WIRDEF_IO_A5C", "profile", "uint8", "0u"),
    ("io_a50", "FLOW_WIRDEF_IO_A50", "profile", "float", "1.0f"),
    ("io_a51", "FLOW_WIRDEF_IO_A51", "profile", "float", "0.0f"),
    ("io_a5p", "FLOW_WIRDEF_IO_A5P", "profile", "int32", "1"),
    ("io_a5n", "FLOW_WIRDEF_IO_A5N", "profile", "float", "Calib::Temperature::Ds18MinValidC"),
    ("io_a5x", "FLOW_WIRDEF_IO_A5X", "profile", "float", "Calib::Temperature::Ds18MaxValidC"),
]


KEY_TO_SPEC: Dict[str, Tuple[str, str, str, str]] = {
    key: (macro, owner, value_type, default_cpp)
    for (key, macro, owner, value_type, default_cpp) in SPECS
}


def _get_project_dir() -> Path:
    if env is not None:
        try:
            return Path(env.get("PROJECT_DIR"))
        except Exception:
            pass
    return Path(os.getcwd())


def _get_project_option(name: str, default: str) -> str:
    if env is None:
        return default
    try:
        return env.GetProjectOption(name, default)
    except Exception:
        return default


def _coerce_bool(value):
    if isinstance(value, bool):
        return value
    if isinstance(value, int) and value in (0, 1):
        return bool(value)
    if isinstance(value, str):
        v = value.strip().lower()
        if v in ("1", "true", "yes", "on"):
            return True
        if v in ("0", "false", "no", "off"):
            return False
    raise ValueError(f"expected bool-compatible value, got {value!r}")


def _coerce_int32(value):
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"expected int32, got {value!r}")
    if value < -2147483648 or value > 2147483647:
        raise ValueError(f"int32 out of range: {value}")
    return value


def _coerce_uint8(value):
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"expected uint8, got {value!r}")
    if value < 0 or value > 255:
        raise ValueError(f"uint8 out of range: {value}")
    return value


def _coerce_float(value):
    if isinstance(value, bool):
        raise ValueError(f"expected float, got {value!r}")
    if not isinstance(value, (int, float)):
        raise ValueError(f"expected float, got {value!r}")
    f = float(value)
    if f != f or f in (float("inf"), float("-inf")):
        raise ValueError(f"invalid float value: {value!r}")
    return f


def _coerce_string(value):
    if not isinstance(value, str):
        raise ValueError(f"expected string, got {value!r}")
    return value


def _format_override(value_type: str, value) -> str:
    if value_type == "bool":
        return "true" if _coerce_bool(value) else "false"
    if value_type == "int32":
        return str(_coerce_int32(value))
    if value_type == "uint8":
        return f"{_coerce_uint8(value)}u"
    if value_type == "float":
        f = _coerce_float(value)
        s = format(f, ".9g")
        if "e" not in s and "E" not in s and "." not in s:
            s += ".0"
        return f"{s}f"
    if value_type == "string":
        return json.dumps(_coerce_string(value))
    raise ValueError(f"unsupported value type: {value_type}")


def _load_overrides(path: Path, required: bool) -> Dict[str, object]:
    if not path.exists():
        if required:
            raise RuntimeError(f"Wokwi defaults file is required but missing: {path}")
        return {}

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Invalid JSON in {path}: {exc}") from exc

    if not isinstance(data, dict):
        raise RuntimeError(f"Expected JSON object in {path}, got {type(data).__name__}")
    return data


def _emit_module_macro(lines: List[str], macro: str, default_cpp: str):
    lines.extend(
        [
            f"#ifndef {macro}",
            f"#define {macro} {default_cpp}",
            "#endif",
            "",
        ]
    )


def _emit_profile_macro(lines: List[str], macro: str, default_cpp: str, override_cpp: str):
    lines.extend(
        [
            f"#ifndef {macro}",
            "#if defined(FLOW_WOKWI_DEFAULT_OVERRIDES)",
            f"#define {macro} {override_cpp}",
            "#else",
            f"#define {macro} {default_cpp}",
            "#endif",
            "#endif",
            "",
        ]
    )


def _write_header(path: Path, overrides: Dict[str, object]):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines: List[str] = [
        "#pragma once",
        "/**",
        " * @file WokwiDefaultOverrides_Generated.h",
        " * @brief Auto-generated module/profile defaults (+ Wokwi profile overrides).",
        " */",
        "",
        "// Generated by scripts/generate_wokwi_default_overrides.py",
        "// Do not edit manually.",
        "",
    ]

    for key, macro, owner, value_type, default_cpp in SPECS:
        if owner == "module":
            _emit_module_macro(lines, macro, default_cpp)
            continue

        override_cpp = default_cpp
        if key in overrides:
            try:
                override_cpp = _format_override(value_type, overrides[key])
            except ValueError as exc:
                raise RuntimeError(f"Invalid value for key '{key}': {exc}") from exc
        _emit_profile_macro(lines, macro, default_cpp, override_cpp)

    path.write_text("\n".join(lines), encoding="utf-8")


def main():
    project_dir = _get_project_dir()
    out_path = project_dir / OUTPUT_REL

    in_opt = _get_project_option("custom_wokwi_defaults_file", str(DEFAULT_INPUT_REL))
    required_opt = _get_project_option("custom_wokwi_defaults_required", "0").strip().lower()
    required = required_opt in ("1", "true", "yes", "on")

    in_path = Path(in_opt)
    if not in_path.is_absolute():
        in_path = project_dir / in_path

    overrides = _load_overrides(in_path, required)

    unknown = sorted(k for k in overrides.keys() if k not in KEY_TO_SPEC)
    if unknown:
        raise RuntimeError("Unknown Wokwi default override key(s): " + ", ".join(unknown))

    blocked = []
    for key in sorted(overrides.keys()):
        _, owner, _, _ = KEY_TO_SPEC[key]
        if owner != "profile":
            blocked.append(key)
    if blocked:
        raise RuntimeError(
            "Wokwi override not allowed for module-owned key(s): " + ", ".join(blocked)
        )

    _write_header(out_path, overrides)
    print(f"[wokwi-defaults] generated {out_path} ({len(overrides)} profile override(s))")


main()
