"""Build a local MainUS.bin from the user's own English bank and checked-in text catalogs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile

from mainus_bank import MainUSBank

LOCALES = ("en", "pt-BR", "es", "fr", "de", "it")


def load_catalog(locale, data, base):
    if locale not in LOCALES or locale == "en":
        raise ValueError(f"unsupported generated locale: {locale}")
    bank = MainUSBank.parse(base)
    known = {record.record_id for record in bank.records}
    if locale == "pt-BR":
        try:
            document = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError("PT-BR catalog is not valid UTF-8 JSON") from error
        digest = hashlib.sha256(base).hexdigest().upper()
        if document.get("formatVersion") != 1:
            raise ValueError("unsupported PT-BR catalog version")
        if document.get("englishBaseSha256", "").upper() != digest:
            raise ValueError("PT-BR catalog does not match this English MainUS bank")
        translated = document.get("translations")
        if not isinstance(translated, dict) or len(translated) != document.get("translatedEntries"):
            raise ValueError("PT-BR catalog count does not match its metadata")
        values = {}
        for key, text in translated.items():
            if not isinstance(key, str) or not re.fullmatch(r"(?:0x)?[0-9a-fA-F]{1,8}", key):
                raise ValueError(f"invalid MainUS ID in PT-BR catalog: {key!r}")
            record_id = int(key, 16)
            if record_id not in known:
                raise ValueError(f"unknown MainUS ID in PT-BR catalog: {key}")
            if not isinstance(text, str) or not text.strip() or "\0" in text:
                raise ValueError(f"empty or invalid PT-BR text for {key}")
            values[record_id] = text
        return values

    values = {}
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ValueError(f"{locale} catalog is not valid UTF-8") from error
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
            key, text = row["id"], row["text"]
            if not isinstance(key, str) or not re.fullmatch(r"(?:0x)?[0-9a-fA-F]{1,8}", key):
                raise ValueError("invalid ID")
            record_id = int(key, 16)
        except (json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
            raise ValueError(f"invalid {locale} catalog row {line_number}: {error}") from error
        if record_id in values:
            raise ValueError(f"duplicate MainUS ID {key} in {locale} catalog")
        if record_id not in known:
            raise ValueError(f"unknown MainUS ID {key} in {locale} catalog")
        if not isinstance(text, str) or not text.strip() or "\0" in text:
            raise ValueError(f"empty or invalid {locale} text for {key}")
        values[record_id] = text
    if set(values) != known:
        raise ValueError(f"{locale} catalog must cover all MainUS IDs (missing {len(known - set(values))})")
    return values


def build_bank(base, locale, catalog_data, output_path=None):
    source = MainUSBank.parse(base)
    translations = load_catalog(locale, catalog_data, base)
    values = {record.record_id: translations.get(record.record_id, record.text) for record in source.records}
    output = source.rebuild(values, index_all=True)
    rebuilt = MainUSBank.parse(output)
    before = {record.record_id: record.text for record in source.records}
    after = {record.record_id: record.text for record in rebuilt.records}
    expected = {record.record_id: values[record.record_id] for record in source.records}
    if len(rebuilt.records) != len(source.records) or rebuilt.indexed_count != len(rebuilt.records):
        raise ValueError("generated locale bank does not index every source record")
    if after != expected or set(after) != set(before):
        raise ValueError("generated locale bank failed ID/text round-trip validation")
    if output_path is not None:
        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        fd, temp_name = tempfile.mkstemp(prefix=output_path.name + ".launcher-tmp-", dir=output_path.parent)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(output)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temp_name, output_path)
        finally:
            if os.path.exists(temp_name):
                os.unlink(temp_name)
    return output


def validate_output_target(base_path, output_path):
    """Reject a generated-bank destination that aliases the user's source bank."""
    base_path = Path(base_path).resolve()
    output_path = Path(output_path).resolve()
    if base_path == output_path:
        raise ValueError("output target is the original English MainUS.bin; choose a separate output folder")
    try:
        if base_path.exists() and output_path.exists() and os.path.samefile(base_path, output_path):
            raise ValueError("output target is the original English MainUS.bin; choose a separate output folder")
    except OSError:
        # A missing output is normal; resolution equality above still covers it.
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True, type=Path, help="user-owned English MainUS.bin")
    parser.add_argument("--catalog-root", required=True, type=Path, help="root containing translations/ and localization/")
    parser.add_argument("--locale", required=True, choices=LOCALES[1:])
    parser.add_argument("--output", required=True, type=Path, help="local output folder; bank is written under language/strings/")
    args = parser.parse_args()
    if args.locale == "pt-BR":
        catalog = args.catalog_root / "translations" / "pt-BR" / "MainUS.json"
    else:
        catalog = args.catalog_root / "localization" / "game" / f"{args.locale}.jsonl"
    target = args.output / "language" / "strings" / "MainUS.bin"
    try:
        validate_output_target(args.base, target)
    except ValueError as error:
        parser.error(str(error))
    result = build_bank(args.base.read_bytes(), args.locale, catalog.read_bytes(), target)
    parsed = MainUSBank.parse(result)
    print(f"Built {args.locale}: {len(parsed.records)} records -> {target}")


if __name__ == "__main__":
    main()
