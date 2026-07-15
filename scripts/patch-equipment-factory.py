#!/usr/bin/env python3
"""Intercept BOTW equipment-child names immediately before actor lookup."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} patch_SpawnActors.asm", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")
    marker = "; MILKBAR_EQUIPMENT_FACTORY_RESOLVER"
    if marker in text:
        return 0

    symbol_anchor = 'HLEName:\n.string "fnCallMain"\n'
    symbol_replacement = (
        symbol_anchor
        + "\nEquipmentResolverName:\n"
        + '.string "ResolveEquipmentActor"\n\n'
        + "EquipmentResolverLoc:\n"
        + ".int 0\n"
    )
    if text.count(symbol_anchor) != 1:
        raise RuntimeError("Expected one spawn HLE name block")
    text = text.replace(symbol_anchor, symbol_replacement, 1)

    hook_anchor = "GetTargetFnRegisters:\n\n; Back up registers we use"
    hook_replacement = (
        "GetTargetFnRegisters:\n\n"
        f"{marker}\n"
        "; NpcEquipment reaches this actor-factory entry before BOTW resolves\n"
        "; the requested child actor. Let the native client replace only the\n"
        "; unique Jugador equipment placeholder buffer while it is still live.\n"
        "addi r1, r1, -8\n"
        "stw r3, 4(r1)\n"
        "mflr r3\n"
        "stw r3, 0(r1)\n"
        "lwz r3, 4(r1)\n"
        "bl UKL_Utils_CreateRegStore\n"
        "addi r1, r1, -4\n"
        "stw r3, 0(r1)\n\n"
        "lis r3, EquipmentResolverName@ha\n"
        "addi r3, r3, EquipmentResolverName@l\n"
        "lis r4, ModuleName@ha\n"
        "addi r4, r4, ModuleName@l\n"
        "lis r5, EquipmentResolverLoc@ha\n"
        "addi r5, r5, EquipmentResolverLoc@l\n"
        "lis r6, ModuleHandle@ha\n"
        "addi r6, r6, ModuleHandle@l\n"
        "lwz r7, 0(r1)\n"
        "bl UKL_Utils_DynamicBranch\n\n"
        "lwz r3, 0(r1)\n"
        "addi r1, r1, 4\n"
        "bl UKL_Utils_LoadRegStore\n"
        "bl UKL_Utils_DisposeRegStore\n"
        "stw r3, 4(r1)\n"
        "lwz r3, 0(r1)\n"
        "mtlr r3\n"
        "lwz r3, 4(r1)\n"
        "addi r1, r1, 8\n\n"
        "; Back up registers we use"
    )
    if text.count(hook_anchor) != 1:
        raise RuntimeError("Expected one actor-factory register hook")
    path.write_text(text.replace(hook_anchor, hook_replacement, 1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
