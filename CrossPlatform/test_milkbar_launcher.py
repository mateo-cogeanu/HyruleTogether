import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

import milkbar_launcher as launcher


class CemuGraphicPackSettingsTests(unittest.TestCase):
    def test_configure_enables_required_packs_once(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            config = Path(temp)
            merged = config / "graphicPacks/BreathOfTheWild_UKMM/rules.txt"
            extended = config / "graphicPacks/downloadedGraphicPacks/BreathOfTheWild/Mods/ExtendedMemory/rules.txt"
            merged.parent.mkdir(parents=True)
            extended.parent.mkdir(parents=True)
            merged.touch()
            extended.touch()

            launcher._configure_cemu_settings(config)
            launcher._configure_cemu_settings(config)

            entries = [
                entry.get("filename")
                for entry in ET.parse(config / "settings.xml").getroot().findall("./GraphicPack/Entry")
            ]
            self.assertEqual(entries.count("graphicPacks/BreathOfTheWild_UKMM/rules.txt"), 1)
            self.assertEqual(
                entries.count(
                    "graphicPacks/downloadedGraphicPacks/BreathOfTheWild/Mods/ExtendedMemory/rules.txt"
                ),
                1,
            )

    def test_missing_extended_memory_pack_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError, "Extended Memory"):
                launcher._require_extended_memory_pack(Path(temp))

    def test_final_ukmm_merge_replaces_incremental_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            merged = root / "merged"
            output = root / "BreathOfTheWild_UKMM"
            title = merged / "content/Pack/TitleBG.pack"
            title.parent.mkdir(parents=True)
            title.write_bytes(b"\0".join(launcher.REQUIRED_MULTIPLAYER_GAME_DATA))
            dlc = merged / "aoc/0010/Pack/Test.pack"
            dlc.parent.mkdir(parents=True)
            dlc.write_bytes(b"merged DLC")
            stale = output / "content/Pack/Stale.pack"
            stale.parent.mkdir(parents=True)
            stale.write_bytes(b"stale")
            (output / "rules.txt").write_text("rules", encoding="utf-8")

            launcher._deploy_final_ukmm_merge(merged, output)

            self.assertFalse(stale.exists())
            self.assertEqual((output / "content/Pack/TitleBG.pack").read_bytes(), title.read_bytes())
            self.assertEqual((output / "aoc/0010/Pack/Test.pack").read_bytes(), b"merged DLC")
            self.assertEqual((output / "rules.txt").read_text(encoding="utf-8"), "rules")

    def test_final_ukmm_merge_requires_animation_controls(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            title = root / "merged/content/Pack/TitleBG.pack"
            title.parent.mkdir(parents=True)
            title.write_bytes(b"Jugador1_Hold only")

            with self.assertRaisesRegex(RuntimeError, "animation controls"):
                launcher._deploy_final_ukmm_merge(root / "merged", root / "output")

    def test_file_contains_all_requires_every_control(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            title = Path(temp) / "TitleBG.pack"
            title.write_bytes(b"Jugador1_Hold\0Jugador1_animationthing")

            self.assertFalse(
                launcher._file_contains_all(title, launcher.REQUIRED_MULTIPLAYER_GAME_DATA)
            )

            with title.open("ab") as output:
                output.write(b"\0Jugador1_AttackAnimation")
            self.assertTrue(
                launcher._file_contains_all(title, launcher.REQUIRED_MULTIPLAYER_GAME_DATA)
            )

if __name__ == "__main__":
    unittest.main()
