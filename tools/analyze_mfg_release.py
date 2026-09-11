"""Offline PE evidence for a supplied MFG DLL/ZIP; never loads or executes it.

Optional analysis dependencies: pip install pefile==2024.8.26 capstone==5.0.7
Example:
  python tools/analyze_mfg_release.py release.zip --output scratch/upstream-213 \
    --rva 8f200 --rva d0910 --rva 97820 --rva a3b8d

An x64 exception-table entry describes an unwind REGION, which can be only a
fragment of an optimized function. RIP-relative string references are leads,
not a complete call graph or proof that a feature executes in a given session.
"""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path


def analyze(source, output, pattern, rvas):
    import capstone
    import pefile
    from capstone.x86 import X86_OP_MEM, X86_REG_RIP

    if zipfile.is_zipfile(source):
        with zipfile.ZipFile(source) as archive:
            candidates = [e for e in archive.infolist()
                          if Path(e.filename).name.lower() == "mapforgoblins.dll"]
            if len(candidates) != 1:
                raise ValueError("Expected exactly one MapForGoblins.dll in archive")
            data = archive.read(candidates[0])
    else:
        data = source.read_bytes()
    pe = pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x8664:
        raise ValueError("This analyzer requires an x64 PE")
    base = pe.OPTIONAL_HEADER.ImageBase
    strings = {}
    for section in pe.sections:
        for match in re.finditer(rb"[\x20-\x7e]{8,}", section.get_data()):
            value = match.group().decode("ascii")
            if pattern.search(value):
                strings[section.VirtualAddress + match.start()] = value
    imports = {i.address - base: i.name.decode() if i.name else f"ordinal {i.ordinal}"
               for d in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []) for i in d.imports}
    regions = [(e.struct.BeginAddress, e.struct.EndAddress)
               for e in getattr(pe, "DIRECTORY_ENTRY_EXCEPTION", [])]
    if not regions:
        raise ValueError("No x64 exception-directory regions available")
    selected = set()
    for rva in rvas:
        matches = [(start, end) for start, end in regions if start <= rva < end]
        if len(matches) != 1:
            raise ValueError(f"RVA {rva:x} does not identify one unwind region")
        selected.add(matches[0][0])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    references = []
    disassembly = {}
    for start, end in regions:
        lines = []
        for ins in md.disasm(pe.get_data(start, end - start), base + start):
            notes = []
            for operand in ins.operands:
                if operand.type != X86_OP_MEM or operand.mem.base != X86_REG_RIP:
                    continue
                target = ins.address + ins.size + operand.mem.disp - base
                if target in strings:
                    references.append({"region_start": hex(start), "region_end": hex(end),
                                       "instruction_rva": hex(ins.address - base),
                                       "string_rva": hex(target), "text": strings[target]})
                notes.append(f"RVA {target:x} {strings.get(target, imports.get(target, ''))}")
            if start in selected:
                lines.append(f"{ins.address-base:08x} {ins.mnemonic:8} {ins.op_str}"
                             + (" ; " + " | ".join(notes) if notes else ""))
        if start in selected:
            disassembly[start] = "\n".join(lines) + "\n"
    report = {
        "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
        "image_base": hex(base), "unwind_regions": len(regions),
        "tools": {"pefile": pefile.__version__, "capstone": capstone.__version__},
        "limitations": "Unwind regions are not necessarily complete functions. "
                       "Only direct RIP-relative references to matching ASCII strings are indexed. "
                       "No runtime or performance claims are established by this report.",
        "references": references,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "evidence.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for start, assembly in disassembly.items():
        (output / f"region_{start:x}.asm").write_text(assembly, encoding="utf-8")
    print(f"{report['sha256']} | {len(data)} bytes | {len(references)} string references")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--match", default=r"\[v3|\[fastmap|\[safemem|\[mapproject")
    parser.add_argument("--rva", action="append", default=[], type=lambda s: int(s, 16))
    args = parser.parse_args()
    analyze(args.source, args.output, re.compile(args.match), args.rva)


if __name__ == "__main__":
    main()
