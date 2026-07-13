#!/usr/bin/env python3
"""Make BOTW actor spawning use an atomic PPC-visible dispatch handshake."""

from pathlib import Path
import sys


def replace_once(text: str, old: str, new: str, description: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"Expected one {description} anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} patch_SpawnActors.asm", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")
    marker = "; MILKBAR_ATOMIC_SPAWN_CLAIM"
    shared_marker = "; MILKBAR_SHARED_SPAWN_ARGUMENTS"
    old_marker = "; MILKBAR_CROSS_CORE_SPAWN_OWNER"

    argument_loads = (
        "; Load the synthetic actor-factory arguments from PPC-visible shared\n"
        "; storage. Unix HLE backends do not consistently write native callback\n"
        "; register edits back to the calling PPC context, and another emulated\n"
        "; core may consume the request. Shared arguments are valid in both cases.\n"
        "lis r3, F_R3@ha\n"
        "lwz r3, F_R3@l(r3)\n"
        "lis r4, F_R4@ha\n"
        "lwz r4, F_R4@l(r4)\n"
        "lis r5, F_R5@ha\n"
        "lwz r5, F_R5@l(r5)\n"
        "lis r6, F_R6@ha\n"
        "lwz r6, F_R6@l(r6)\n"
        "lis r7, F_R7@ha\n"
        "lwz r7, F_R7@l(r7)\n"
        "lis r8, F_R8@ha\n"
        "lwz r8, F_R8@l(r8)\n"
        "lis r9, F_R9@ha\n"
        "lwz r9, F_R9@l(r9)\n"
        "lis r10, F_R10@ha\n"
        "lwz r10, F_R10@l(r10)\n"
    )

    atomic_claim = (
        "; Enabled is published only after every shared argument is visible.\n"
        "lis r11, Enabled@ha\n"
        "lbz r11, Enabled@l(r11)\n"
        "cmpwi r11, 0\n"
        "beq restoreAndExit\n"
        "; Atomically claim the shared request. CallFunction runs on all three\n"
        "; emulated PPC cores, where the old byte-only flag allowed duplicate dispatch.\n"
        "; the same actor. State 1 is ready and state 2 has one owner.\n"
        "lis r12, SpawnDispatchState@ha\n"
        "addi r12, r12, SpawnDispatchState@l\n"
        "claimSpawnRequest:\n"
        "lwarx r11, 0, r12\n"
        "cmpwi r11, 1\n"
        "bne restoreAndExit\n"
        "li r11, 2\n"
        "stwcx. r11, 0, r12\n"
        "bne claimSpawnRequest\n"
    )
    dispatch_complete = (
        "dispatchComplete:\n"
        "; Keep Enabled asserted until the actor factory returns so the native\n"
        "; callback cannot overwrite the winning request while it is in flight.\n"
        "li r11, 0\n"
        "lis r12, SpawnDispatchState@ha\n"
        "stw r11, SpawnDispatchState@l(r12)\n"
        "lis r12, Enabled@ha\n"
        "stb r11, Enabled@l(r12)\n"
        "b restoreAndExit\n\n"
    )

    if marker in text:
        return 0

    if shared_marker in text:
        text = text.replace(shared_marker, marker)
        text = text.replace("SpawnDispatchReserved", "SpawnDispatchState")
        text = text.replace(
            "; Reserved transfer word retained to keep the native/PPC ABI stable.",
            "; Atomic state for single-consumer dispatch (0 idle, 1 ready, 2 claimed).",
        )
        old_ready_check = (
            "; Set up if statment\n"
            "lis r11, Enabled@ha\n"
            "lbz r11, Enabled@l(r11)\n"
            "cmpwi r11, 0x0\n"
            "beq restoreAndExit ; We can skip over applying params if we're not calling the func, so putting this here is fine.\n"
        )
        text = replace_once(text, old_ready_check, atomic_claim, "shared spawn-ready check")
        text = replace_once(
            text,
            "lis r11, restoreAndExit@ha\naddi r11, r11, restoreAndExit@l",
            "lis r11, dispatchComplete@ha\naddi r11, r11, dispatchComplete@l",
            "spawn return target",
        )
        old_clear = (
            "; Set stuff to false so we don't infinitly call this\n"
            "li r11, 0x0\n"
            "lis r12, Enabled@ha\n"
            "stb r11, Enabled@l(r12)\n\n"
        )
        text = replace_once(text, old_clear, "", "non-atomic spawn-ready clear")
        text = replace_once(text, "restoreAndExit:\n", dispatch_complete + "restoreAndExit:\n", "dispatch completion target")
        path.write_text(text, encoding="utf-8")
        return 0

    if old_marker in text:
        text = text.replace(old_marker, shared_marker)
        text = text.replace(
            "; Explicit r12 token of the CallFunction invocation that owns the staged GPRs.\n"
            "SpawnOwnerToken:\n.int 0",
            "; Reserved transfer word retained to keep the native/PPC ABI stable.\n"
            "SpawnDispatchReserved:\n.int 0",
        )
        old_owner_check = (
            "; HLE callbacks may overlap on Cemu's three PPC cores. The staged GPRs\n"
            "; belong only to the invocation carrying the matching saved r12 token.\n"
            "lis r11, SpawnOwnerToken@ha\n"
            "lwz r11, SpawnOwnerToken@l(r11)\n"
            "cmpw r12, r11\n"
            "bne restoreAndExit\n"
        )
        text = replace_once(text, old_owner_check, argument_loads, "old spawn-owner check")
        # Continue through the shared-handshake migration below.
        text = text.replace(shared_marker, marker)
        text = text.replace("SpawnDispatchReserved", "SpawnDispatchState")
        text = text.replace(
            "; Reserved transfer word retained to keep the native/PPC ABI stable.",
            "; Atomic state for single-consumer dispatch (0 idle, 1 ready, 2 claimed).",
        )
        old_ready_prefix = (
            "; Set up if statment\n"
            "lis r11, Enabled@ha\n"
            "lbz r11, Enabled@l(r11)\n"
            "cmpwi r11, 0x0\n"
            "beq restoreAndExit ; We can skip over applying params if we're not calling the func, so putting this here is fine.\n"
        )
        text = replace_once(text, old_ready_prefix, atomic_claim, "spawn-ready check")
        text = replace_once(
            text,
            "lis r11, restoreAndExit@ha\naddi r11, r11, restoreAndExit@l",
            "lis r11, dispatchComplete@ha\naddi r11, r11, dispatchComplete@l",
            "spawn return target",
        )
        old_clear = (
            "; Set stuff to false so we don't infinitly call this\n"
            "li r11, 0x0\n"
            "lis r12, Enabled@ha\n"
            "stb r11, Enabled@l(r12)\n\n"
        )
        text = replace_once(text, old_clear, "", "non-atomic spawn-ready clear")
        text = replace_once(text, "restoreAndExit:\n", dispatch_complete + "restoreAndExit:\n", "dispatch completion target")
        path.write_text(text, encoding="utf-8")
        return 0

    text = replace_once(
        text,
        "InterceptRegisters:\n.byte 0\n\nFunctionToJump:",
        "InterceptRegisters:\n.byte 0\n\n"
        f"{marker}\n"
        "; Atomic state for single-consumer dispatch (0 idle, 1 ready, 2 claimed).\n"
        "SpawnDispatchState:\n.int 0\n\n"
        "FunctionToJump:",
        "spawn-transfer layout",
    )
    text = replace_once(
        text,
        "beq restoreAndExit ; We can skip over applying params if we're not calling the func, so putting this here is fine.\n\n"
        "; Set up where to jump to...",
        "beq restoreAndExit ; We can skip over applying params if we're not calling the func, so putting this here is fine.\n\n"
        f"{atomic_claim}\n{argument_loads}\n"
        "; Set up where to jump to...",
        "spawn request consumer",
    )
    text = replace_once(
        text,
        "lis r11, restoreAndExit@ha\naddi r11, r11, restoreAndExit@l",
        "lis r11, dispatchComplete@ha\naddi r11, r11, dispatchComplete@l",
        "spawn return target",
    )
    old_clear = (
        "; Set stuff to false so we don't infinitly call this\n"
        "li r11, 0x0\n"
        "lis r12, Enabled@ha\n"
        "stb r11, Enabled@l(r12)\n\n"
    )
    text = replace_once(text, old_clear, "", "non-atomic spawn-ready clear")
    text = replace_once(text, "restoreAndExit:\n", dispatch_complete + "restoreAndExit:\n", "dispatch completion target")
    path.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
