#!/usr/bin/env python3
"""
Generate static C++ element spectral data from the Max/Cosmic Unity data files.

The plugin must not read spectral CSV/TXT files from the realtime audio path.
This script turns data/*.txt into Source/ElementSpectralData.cpp so the existing
AtomicScaleBuilder can keep using immutable in-process source lines.
"""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"
HEADER_PATH = ROOT / "Source" / "ElementSpectralData.h"
CPP_PATH = ROOT / "Source" / "ElementSpectralData.cpp"

ELEMENTS = [
    ("H", "Hydrogen"),
    ("He", "Helium"),
    ("Li", "Lithium"),
    ("Be", "Beryllium"),
    ("B", "Boron"),
    ("C", "Carbon"),
    ("O", "Oxygen"),
    ("F", "Fluorine"),
    ("Ne", "Neon"),
    ("Na", "Sodium"),
    ("Mg", "Magnesium"),
    ("Al", "Aluminium"),
    ("Si", "Silicon"),
    ("P", "Phosphorus"),
    ("S", "Sulfur"),
    ("Cl", "Chlorine"),
    ("Ar", "Argon"),
    ("K", "Potassium"),
    ("Ca", "Calcium"),
    ("Sc", "Scandium"),
    ("Ti", "Titanium"),
    ("V", "Vanadium"),
    ("Cr", "Chromium"),
    ("Mn", "Manganese"),
    ("Fe", "Iron"),
    ("Co", "Cobalt"),
    ("Ni", "Nickel"),
    ("Cu", "Copper"),
    ("Zn", "Zinc"),
]


def function_name(symbol: str) -> str:
    return symbol.lower() + "Lines"


def parse_lines(symbol: str) -> list[tuple[float, float]]:
    path = DATA_DIR / f"{symbol}.txt"
    if not path.exists():
        raise FileNotFoundError(path)

    rows: list[tuple[float, float]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        parts = stripped.split()
        if len(parts) < 2:
            continue

        try:
            wavelength = float(parts[0])
            intensity = float(parts[1])
        except ValueError as exc:
            raise ValueError(f"{path}:{line_number}: expected wavelength intensity") from exc

        rows.append((wavelength, intensity))

    if not rows:
        raise ValueError(f"{path} did not contain any spectral lines")

    return rows


def write_header() -> None:
    HEADER_PATH.write_text(
        """#pragma once

#include "AtomicScaleBuilder.h"

#include <vector>

namespace ElementSpectralData
{
    int numSupportedElements() noexcept;
    const char* symbolForElement (int elementIndex) noexcept;
    const char* nameForElement (int elementIndex) noexcept;
    const std::vector<AtomicScaleBuilder::SourceLine>& linesForElement (int elementIndex);
}
""",
        encoding="utf-8",
    )


def write_cpp() -> None:
    out: list[str] = []
    out.append('#include "ElementSpectralData.h"\n\n')
    out.append("namespace ElementSpectralData\n{\n")
    out.append("namespace\n{\n")
    out.append("    using Unit = AtomicScaleBuilder::WavelengthUnit;\n\n")

    for symbol, name in ELEMENTS:
        rows = parse_lines(symbol)
        out.append(f"    const std::vector<AtomicScaleBuilder::SourceLine>& {function_name(symbol)}()\n")
        out.append("    {\n")
        out.append("        static const std::vector<AtomicScaleBuilder::SourceLine> lines {\n")
        for index, (wavelength, intensity) in enumerate(rows, 1):
            wavelength_label = f"{wavelength:.3f}"
            line_id = f"{symbol}-{index:04d}-{wavelength_label}"
            label = f"{symbol} {wavelength_label} nm"
            out.append(
                f'            {{ "{line_id}", "{label}", '
                f"{wavelength:.12g}, Unit::Nanometer, {intensity:.12g} }},\n"
            )
        out.append("        };\n")
        out.append("        return lines;\n")
        out.append("    }\n\n")

    out.append("    const std::vector<AtomicScaleBuilder::SourceLine>& emptyLines()\n")
    out.append("    {\n")
    out.append("        static const std::vector<AtomicScaleBuilder::SourceLine> lines;\n")
    out.append("        return lines;\n")
    out.append("    }\n")
    out.append("}\n\n")

    out.append("int numSupportedElements() noexcept\n")
    out.append("{\n")
    out.append(f"    return {len(ELEMENTS)};\n")
    out.append("}\n\n")

    out.append("const char* symbolForElement (int elementIndex) noexcept\n")
    out.append("{\n")
    out.append("    switch (elementIndex)\n")
    out.append("    {\n")
    for index, (symbol, _) in enumerate(ELEMENTS):
        out.append(f'        case {index}: return "{symbol}";\n')
    out.append('        default: return "";\n')
    out.append("    }\n")
    out.append("}\n\n")

    out.append("const char* nameForElement (int elementIndex) noexcept\n")
    out.append("{\n")
    out.append("    switch (elementIndex)\n")
    out.append("    {\n")
    for index, (_, name) in enumerate(ELEMENTS):
        out.append(f'        case {index}: return "{name}";\n')
    out.append('        default: return "";\n')
    out.append("    }\n")
    out.append("}\n\n")

    out.append("const std::vector<AtomicScaleBuilder::SourceLine>& linesForElement (int elementIndex)\n")
    out.append("{\n")
    out.append("    switch (elementIndex)\n")
    out.append("    {\n")
    for index, (symbol, _) in enumerate(ELEMENTS):
        out.append(f"        case {index}: return {function_name(symbol)}();\n")
    out.append("        default: return emptyLines();\n")
    out.append("    }\n")
    out.append("}\n")
    out.append("}\n")

    CPP_PATH.write_text("".join(out), encoding="utf-8")


def main() -> None:
    write_header()
    write_cpp()
    print(f"Generated {HEADER_PATH}")
    print(f"Generated {CPP_PATH}")


if __name__ == "__main__":
    main()
