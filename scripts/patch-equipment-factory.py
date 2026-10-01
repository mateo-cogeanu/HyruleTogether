#!/usr/bin/env python3
"""Intercept BOTW equipment-child names immediately before actor lookup."""

from pathlib import Path
import sys


def add_item_factory_hook(text: str) -> str:
    marker = '; HYRULE_INVENTORY_ITEM_FACTORY_V208'
    if marker in text:
        return text
    start = text.index('EquipmentChildCreateHook:\n')
    end = text.index('b 0x037b5be4\n', start) + len('b 0x037b5be4\n')
    hook = text[start:end].replace('EquipmentChildCreateHook:', 'InventoryItemCreateHook:')
    hook = hook.replace('stwu r1, -40(r1)', 'stwu r1, -48(r1)')
    hook = hook.replace('b 0x037b5be4', 'b 0x037b5e90')
    return text + ('\n\n' + marker + '\n'
                   '; Inventory drops enter the v208 create wrapper with r4=name\n'
                   '; and r7=InstParamPack. Preserve its original stack prologue.\n'
                   '0x037b5e8c = b InventoryItemCreateHook\n\n' + hook)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} patch_SpawnActors.asm", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")
    marker = "; MILKBAR_EQUIPMENT_FACTORY_RESOLVER"
    internal_marker = "; MILKBAR_INTERNAL_EQUIPMENT_FACTORY_RESOLVER_V3"
    if internal_marker in text:
        path.write_text(add_item_factory_hook(text), encoding="utf-8")
        return 0

    if marker not in text:
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

    resolver_call = (
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
        "addi r1, r1, 8\n"
    )

    if marker not in text:
        hook_anchor = "GetTargetFnRegisters:\n\n; Back up registers we use"
        hook_replacement = (
            "GetTargetFnRegisters:\n\n"
            f"{marker}\n"
            "; Resolve placeholders submitted through the public ActorCreator wrapper.\n"
            + resolver_call
            + "\n; Back up registers we use"
        )
        if text.count(hook_anchor) != 1:
            raise RuntimeError("Expected one actor-factory register hook")
        text = text.replace(hook_anchor, hook_replacement, 1)

    internal_anchor = "0x037b6040 = b GetTargetFnRegisters\n"
    internal_hook = (
        "; NpcEquipment bypasses the public wrapper above and enters ActorCreator's\n"
        "; internal createActor_ wrapper at 0x037b5be0. At this point r4 is the\n"
        "; requested child name and has not yet been looked up.\n"
        f"{internal_marker}\n"
        "0x037b5be0 = b EquipmentChildCreateHook\n\n"
        "EquipmentChildCreateHook:\n"
        + resolver_call
        + "; Execute the replaced createActor_ prologue and return to the next instruction.\n"
        "stwu r1, -40(r1)\n"
        "b 0x037b5be4\n\n"
        + internal_anchor
    )
    if text.count(internal_anchor) != 1:
        raise RuntimeError("Expected one public actor-factory hook assignment")
    path.write_text(add_item_factory_hook(text.replace(internal_anchor, internal_hook, 1)), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
