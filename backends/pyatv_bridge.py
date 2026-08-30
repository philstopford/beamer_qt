#!/usr/bin/env python3
"""Small JSON-lines bridge around pyatv.

The Qt process can keep this backend optional while we validate AirPlay 2 on
real hardware. One request and one JSON response are exchanged per line.
"""
import asyncio
import json
import sys
import pyatv
from pyatv.const import Protocol
from pyatv.storage.file_storage import FileStorage

STORAGE = ".beamer-pyatv.json"


async def main(request):
    command = request.get("command")
    if command == "scan":
        devices = await pyatv.scan(asyncio.get_running_loop(), timeout=5)
        return {"devices": [{"id": d.identifier, "name": d.name,
                             "address": str(d.address),
                             "services": [s.protocol.name for s in d.services]}
                            for d in devices]}

    storage = FileStorage(STORAGE, asyncio.get_running_loop())
    config = await pyatv.scan(asyncio.get_running_loop(), timeout=5, storage=storage)
    identifier = request["id"]
    device = next((d for d in config if d.identifier == identifier), None)
    if device is None:
        return {"error": "device not found"}
    if command == "pair":
        handler = await pyatv.pair(device, Protocol.AirPlay, asyncio.get_running_loop(), storage=storage)
        await handler.begin()
        handler.pin(int(request["pin"]))
        await handler.finish()
        await handler.close()
        return {"ok": True}
    # The default service can be Companion/MRP, where play_url is not
    # implemented. Local media playback must use the AirPlay service.
    atv = await pyatv.connect(device, asyncio.get_running_loop(), protocol=Protocol.AirPlay, storage=storage)
    try:
        if command == "play":
            return {"error": "pyatv does not implement AirPlay video URL playback; it only provides Apple TV discovery, pairing, and control"}
        elif command == "pause":
            await atv.remote_control.pause()
        elif command == "stop":
            await atv.remote_control.stop()
        elif command == "volume":
            await atv.audio.set_volume(float(request["value"]))
        else:
            return {"error": "unknown command"}
        return {"ok": True}
    finally:
        # AppleTV.close() is synchronous in pyatv 0.18 and returns its
        # outstanding task set; awaiting it raises "set can't be awaited".
        atv.close()


for line in sys.stdin:
    try:
        print(json.dumps(asyncio.run(main(json.loads(line)))), flush=True)
    except Exception as error:
        print(json.dumps({"error": str(error)}), flush=True)
