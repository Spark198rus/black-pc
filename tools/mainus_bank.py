"""Read and rebuild the BLACK MainUS string bank without changing record IDs."""

from dataclasses import dataclass
import struct
from typing import Dict, List


def _align4(value: int) -> int:
    return (value + 3) & ~3


@dataclass(frozen=True)
class StringRecord:
    record_id: int
    text: str


@dataclass
class MainUSBank:
    prefix: bytes
    table_offset: int
    indexed_count: int
    records: List[StringRecord]
    trailing_bytes: bytes

    @classmethod
    def parse(cls, data: bytes) -> "MainUSBank":
        if len(data) < 16:
            raise ValueError("MainUS bank is shorter than its header")
        indexed_count = struct.unpack_from("<H", data, 0)[0]
        total_count = struct.unpack_from("<I", data, 8)[0]
        table_offset = struct.unpack_from("<I", data, 12)[0]
        table_end = table_offset + indexed_count * 4
        if indexed_count == 0 or total_count < indexed_count or table_end > len(data):
            raise ValueError("MainUS bank has invalid record counts or pointer table")

        offsets = list(struct.unpack_from("<" + "I" * indexed_count, data, table_offset))
        if offsets != sorted(set(offsets)):
            raise ValueError("MainUS indexed record offsets are not unique and ordered")

        records: List[StringRecord] = []
        last_end = 0
        for index, offset in enumerate(offsets):
            limit = offsets[index + 1] if index + 1 < len(offsets) else len(data)
            record, end = cls._read_record(data, offset, limit)
            records.append(record)
            last_end = max(last_end, end)

        cursor = _align4(last_end)
        last_record_end = last_end
        for _ in range(total_count - indexed_count):
            record, end = cls._read_record(data, cursor, len(data))
            records.append(record)
            last_record_end = end
            cursor = _align4(end)
        if len({record.record_id for record in records}) != total_count:
            raise ValueError("MainUS bank contains duplicate record IDs")
        if cursor not in (len(data), _align4(len(data))) and any(data[cursor:]):
            raise ValueError("MainUS bank has unparsed trailing data")
        return cls(data[:offsets[0]], table_offset, indexed_count, records, data[last_record_end:])

    @staticmethod
    def _read_record(data: bytes, offset: int, limit: int):
        if offset < 0 or offset + 5 > len(data) or offset + 5 > limit:
            raise ValueError("MainUS record starts outside its data region")
        record_id = struct.unpack_from("<I", data, offset)[0]
        end = data.find(b"\0", offset + 4, min(limit, len(data)))
        if end < 0:
            raise ValueError("MainUS record has no UTF-8 terminator")
        try:
            text = data[offset + 4:end].decode("utf-8")
        except UnicodeDecodeError as error:
            raise ValueError("MainUS record is not valid UTF-8") from error
        return StringRecord(record_id, text), end + 1

    def rebuild(self, translations: Dict[int, str], index_all: bool = False) -> bytes:
        missing = [record.record_id for record in self.records if record.record_id not in translations]
        if missing:
            raise ValueError("Missing translations for record IDs: " + ", ".join(hex(i) for i in missing[:8]))

        if index_all:
            indexed_records = sorted(
                self.records,
                key=lambda record: record.record_id if record.record_id < 0x80000000
                else record.record_id - 0x100000000,
            )
            tail_records = []
            table_end = self.table_offset + len(indexed_records) * 4
            output = bytearray(_align4(table_end))
            output[:self.table_offset] = self.prefix[:self.table_offset]
            struct.pack_into("<H", output, 0, len(indexed_records))
        else:
            indexed_records = self.records[:self.indexed_count]
            tail_records = self.records[self.indexed_count:]
            output = bytearray(self.prefix)

        cursor = len(output)
        for index, record in enumerate(indexed_records):
            struct.pack_into("<I", output, self.table_offset + index * 4, cursor)
            pad = index + 1 < len(self.records)
            cursor = self._write_record(output, record.record_id, translations[record.record_id], pad)
        for index, record in enumerate(tail_records):
            pad = index + 1 < len(tail_records)
            cursor = self._write_record(output, record.record_id, translations[record.record_id], pad)
        output.extend(self.trailing_bytes)
        return bytes(output)

    @staticmethod
    def _write_record(output: bytearray, record_id: int, text: str, pad: bool = True) -> int:
        if "\0" in text:
            raise ValueError("MainUS text cannot contain a NUL character")
        output.extend(struct.pack("<I", record_id))
        output.extend(text.encode("utf-8") + b"\0")
        if pad:
            while len(output) % 4:
                output.append(0)
        return len(output)
