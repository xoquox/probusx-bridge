"""Ruft die lokale HTTP-Schnittstelle auf. Home Assistant greift nicht auf den Bus zu."""

import aiohttp
from yarl import URL
from .codec import validate


class Client:
    def __init__(self, session, host, port=80):
        self.session = session
        self.base = URL.build(scheme="http", host=host, port=port)
        self.token = None

    async def state(self):
        async with self.session.get(
            self.base.with_path("/api/live"),
            timeout=aiohttp.ClientTimeout(total=3),
            allow_redirects=False,
        ) as response:
            response.raise_for_status()
            data = validate(await response.json())
        self.token = data.get("token")
        return data

    async def display(self):
        async with self.session.get(
            self.base.with_path("/api/display"),
            timeout=aiohttp.ClientTimeout(total=2),
            allow_redirects=False,
        ) as response:
            response.raise_for_status()
            payload = await response.content.readexactly(5120)
            if await response.content.read(1):
                raise ValueError("Displaydaten sind zu lang")
            return payload

    async def press(self, key):
        # Sitzungstoken neu abrufen. Einen möglicherweise ausgeführten Tastendruck nicht wiederholen.
        await self.state()
        async with self.session.post(
            self.base.with_path("/api/key"),
            data={"token": self.token, "key": key},
            timeout=aiohttp.ClientTimeout(total=3),
            allow_redirects=False,
        ) as response:
            result = await response.json()
            if response.status != 200 or not result.get("accepted"):
                raise ValueError(result.get("error", "Taste abgelehnt"))

    async def diagnostics(self):
        async with self.session.get(
            self.base.with_path("/api/diagnostics"),
            timeout=aiohttp.ClientTimeout(total=3),
            allow_redirects=False,
        ) as response:
            response.raise_for_status()
            return await response.json()
