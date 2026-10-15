"""Prüft die lokale Bridge-Adresse und richtet genau einen Eintrag je Gerätekennung ein."""

import asyncio
import ipaddress
import re
import aiohttp
import voluptuous as vol
from homeassistant import config_entries
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from .client import Client
from .const import DOMAIN


def valid_host(host):
    if not isinstance(host, str) or not host or len(host) > 253:
        raise vol.Invalid("IP-Adresse oder Hostname ohne http:// eingeben")
    try:
        ipaddress.ip_address(host)
    except ValueError:
        if not re.fullmatch(r"[a-zA-Z0-9](?:[a-zA-Z0-9.\-]*[a-zA-Z0-9])?", host):
            raise vol.Invalid("Ungültiger Hostname")
    return host


class ProBusXFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        errors = {}
        if user_input is not None:
            try:
                host = valid_host(user_input["host"].strip())
                data = await Client(
                    async_get_clientsession(self.hass), host, user_input["port"]
                ).state()
            except (aiohttp.ClientError, asyncio.TimeoutError):
                errors["base"] = "cannot_connect"
            except (ValueError, vol.Invalid):
                errors["base"] = "invalid_firmware"
            else:
                await self.async_set_unique_id(data["device_id"])
                self._abort_if_unique_id_configured(
                    updates={"host": host, "port": user_input["port"]}
                )
                return self.async_create_entry(
                    title="ProBusX", data={"host": host, "port": user_input["port"]}
                )
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required("host"): str,
                    vol.Required("port", default=80): vol.All(
                        vol.Coerce(int), vol.Range(min=1, max=65535)
                    ),
                }
            ),
            errors=errors,
        )
