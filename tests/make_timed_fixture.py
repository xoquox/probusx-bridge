"""Wandelt die JSONL-Aufnahme mit Hexdaten in die Eingabe für den Zeittest um."""

import argparse
import json
import struct

p = argparse.ArgumentParser()
p.add_argument("jsonl")
p.add_argument("output")
args = p.parse_args()
with open(args.jsonl) as source, open(args.output, "wb") as target:
    for line in source:
        item = json.loads(line)
        if item["type"] != "data":
            continue
        raw = bytes.fromhex(item["hex"])
        target.write(struct.pack("<QI", item["esp_time_us"], len(raw)) + raw)
