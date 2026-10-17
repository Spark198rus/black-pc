import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from build_language_bank import build_bank, load_catalog, validate_output_target


def bank_fixture(records):
    table_offset = 16
    bank = bytearray(table_offset + len(records) * 4)
    struct.pack_into("<H", bank, 0, len(records))
    struct.pack_into("<I", bank, 8, len(records))
    struct.pack_into("<I", bank, 12, table_offset)
    for i, (record_id, text) in enumerate(records):
        struct.pack_into("<I", bank, table_offset + i * 4, len(bank))
        bank.extend(struct.pack("<I", record_id) + text.encode("utf-8") + b"\0")
        if i + 1 < len(records):
            while len(bank) % 4:
                bank.append(0)
    return bytes(bank)


class LanguageBankTests(unittest.TestCase):
    def test_sparse_ptbr_preserves_english_fallback_and_record_keys(self):
        base = bank_fixture([(0x10, "Start"), (0x20, "Options"), (0x30, "Quit")])
        catalog = {"formatVersion": 1, "englishBaseSha256": hashlib.sha256(base).hexdigest().upper(),
                   "translatedEntries": 1, "translations": {"0x00000020": "Opções"}}
        encoded = json.dumps(catalog, ensure_ascii=False).encode("utf-8")
        out = build_bank(base, "pt-BR", encoded)
        records = load_catalog("pt-BR", encoded, base)
        self.assertEqual(records, {0x20: "Opções"})
        from mainus_bank import MainUSBank
        result = MainUSBank.parse(out)
        self.assertEqual([(r.record_id, r.text) for r in result.records],
                         [(0x10, "Start"), (0x20, "Opções"), (0x30, "Quit")])
        self.assertEqual(result.indexed_count, len(result.records))

    def test_complete_jsonl_locale_replaces_every_record(self):
        base = bank_fixture([(0x10, "Start"), (0x20, "Options")])
        catalog = b'{"id":"0x00000010","text":"Comenzar"}\n{"id":"0x00000020","text":"Opciones"}\n'
        values = load_catalog("es", catalog, base)
        self.assertEqual(values, {0x10: "Comenzar", 0x20: "Opciones"})

    def test_rejects_duplicate_and_unknown_ids(self):
        base = bank_fixture([(0x10, "Start")])
        for body in (
            b'{"id":"0x10","text":"A"}\n{"id":"0x10","text":"B"}\n',
            b'{"id":"0x99","text":"Unknown"}\n',
        ):
            with self.subTest(body=body), self.assertRaises(ValueError):
                load_catalog("es", body, base)

    def test_rejects_wrong_ptbr_base_hash_and_preserves_existing_output(self):
        base = bank_fixture([(0x10, "Start")])
        catalog = {"formatVersion": 1, "englishBaseSha256": "0" * 64,
                   "translatedEntries": 0, "translations": {}}
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "MainUS.bin"
            output.write_bytes(b"previous valid output")
            with self.assertRaises(ValueError):
                build_bank(base, "pt-BR", json.dumps(catalog).encode("utf-8"), output)
            self.assertEqual(output.read_bytes(), b"previous valid output")

    def test_refuses_original_bank_as_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp) / "game" / "language" / "strings" / "MainUS.bin"
            base.parent.mkdir(parents=True)
            original = bank_fixture([(0x10, "Start")])
            base.write_bytes(original)
            output = base.parent / "." / "MainUS.bin"
            with self.assertRaisesRegex(ValueError, "original English MainUS.bin"):
                validate_output_target(base, output)
            self.assertEqual(base.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
