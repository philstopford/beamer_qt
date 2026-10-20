#!/usr/bin/env python3
"""JSON-lines bridge around pyatv and AirPlay direct control.

Provides fallback discovery, pairing, probe, and remote control for Apple TV
when the native airfry helper is not compiled. One request and one JSON response
are exchanged per line on standard input/output.
"""
import asyncio
import json
import os
import plistlib
import subprocess
import sys
import urllib.request
import urllib.error

STORAGE = ".beamer-pyatv.json"


def http_airplay_probe(address, port=7000):
    url = f"http://{address}:{port}/info"
    # First try urllib with AirPlay headers
    try:
        req = urllib.request.Request(url, headers={
            "User-Agent": "AirPlay/935.7.1",
            "Content-Type": "application/x-apple-binary-plist"
        })
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            data = resp.read()
            pl = plistlib.loads(data)
            return {"ok": True, "info": f"{pl.get('model', 'AppleTV')} ({pl.get('name', 'Apple TV')}) at {address}:{port}"}
    except Exception:
        pass

    # Fallback to curl on macOS/Linux if urllib is restricted by network policies
    try:
        res = subprocess.run([
            "curl", "-s", "--connect-timeout", "3",
            "-H", "User-Agent: AirPlay/935.7.1",
            "-H", "Content-Type: application/x-apple-binary-plist",
            url
        ], capture_output=True, timeout=5)
        if res.returncode == 0 and res.stdout:
            try:
                pl = plistlib.loads(res.stdout)
                return {"ok": True, "info": f"{pl.get('model', 'AppleTV')} ({pl.get('name', 'Apple TV')}) at {address}:{port}"}
            except Exception:
                return {"ok": True, "info": f"AirPlay receiver active at {address}:{port}"}
    except Exception as e:
        return {"error": str(e)}

    return {"error": f"Could not connect to AirPlay service at {address}:{port}"}


def http_pair_pin_start(address, port=7000):
    url = f"http://{address}:{port}/pair-pin-start"
    headers = {
        "User-Agent": "AirPlay/935.7.1",
        "X-Apple-HKP": "3",
        "Content-Type": "application/octet-stream"
    }
    # Try urllib first
    try:
        req = urllib.request.Request(url, data=b"", headers=headers, method="POST")
        with urllib.request.urlopen(req, timeout=4.0) as resp:
            if resp.status in (200, 204):
                return {"ok": True}
    except Exception:
        pass

    # Fallback to curl
    try:
        res = subprocess.run([
            "curl", "-s", "--connect-timeout", "4",
            "-X", "POST", url,
            "-H", "User-Agent: AirPlay/935.7.1",
            "-H", "X-Apple-HKP: 3",
            "-H", "Content-Type: application/octet-stream",
            "--data-binary", ""
        ], capture_output=True, timeout=6)
        if res.returncode == 0:
            return {"ok": True}
        return {"error": f"curl pair-pin-start returned code {res.returncode}"}
    except Exception as e:
        return {"error": str(e)}


async def handle_request(request):
    command = request.get("command")
    address = request.get("address")
    identifier = request.get("id")

    if command == "probe":
        if not address:
            return {"error": "address is required for probe"}
        return http_airplay_probe(address)

    if command in ("pair_start", "pair-pin-start"):
        if not address:
            return {"error": "address is required for pair_start"}
        return http_pair_pin_start(address)

    if command == "credentials-check":
        if os.path.exists(STORAGE):
            return {"ok": True, "paired": True}
        return {"ok": True, "paired": False}

    if command == "credentials-reset":
        if os.path.exists(STORAGE):
            try:
                os.remove(STORAGE)
            except OSError as e:
                return {"error": str(e)}
        return {"ok": True}

    # Commands below require pyatv
    try:
        import pyatv
        from pyatv.const import Protocol
        from pyatv.storage.file_storage import FileStorage
    except ImportError:
        if command == "scan":
            return {"devices": []}
        return {"error": "pyatv is not installed. Install with 'pip install pyatv' or compile the native airfry helper."}

    loop = asyncio.get_running_loop()

    if command == "scan":
        devices = await pyatv.scan(loop, timeout=5)
        return {"devices": [{"id": d.identifier, "name": d.name,
                             "address": str(d.address),
                             "services": [s.protocol.name for s in d.services]}
                            for d in devices]}

    storage = FileStorage(STORAGE, loop)
    hosts = [address] if address else None
    config = await pyatv.scan(loop, timeout=5, storage=storage, hosts=hosts)
    device = None
    if identifier:
        device = next((d for d in config if d.identifier == identifier), None)
    if device is None and address:
        device = next((d for d in config if str(d.address) == str(address)), None)
    if device is None and config:
        device = config[0]

    if device is None:
        return {"error": "device not found"}

    if command == "pair":
        handler = await pyatv.pair(device, Protocol.AirPlay, loop, storage=storage)
        try:
            await handler.begin()
            handler.pin(int(request.get("pin", 0)))
            await handler.finish()
            return {"ok": True}
        finally:
            await handler.close()

    atv = await pyatv.connect(device, loop, protocol=Protocol.AirPlay, storage=storage)
    try:
        if command == "play":
            return {"error": "pyatv does not implement AirPlay video URL playback; use the native airfry backend for video streaming"}
        elif command == "pause":
            await atv.remote_control.pause()
        elif command == "stop":
            await atv.remote_control.stop()
        elif command == "volume":
            await atv.audio.set_volume(float(request.get("value", 0.0)))
        else:
            return {"error": f"unknown command: {command}"}
        return {"ok": True}
    finally:
        atv.close()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            res = asyncio.run(handle_request(req))
            print(json.dumps(res), flush=True)
        except Exception as error:
            print(json.dumps({"error": str(error)}), flush=True)


if __name__ == "__main__":
    main()
