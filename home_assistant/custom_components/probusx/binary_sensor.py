"""Meldet einen gültigen Ausgangsstellwert größer Null als eingeschaltet."""

from homeassistant.components.binary_sensor import BinarySensorEntity
from .const import OUTPUTS
from .codec import output
from .entity import ProBusXEntity


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([Active(entry.runtime_data, i, name) for i, name in enumerate(OUTPUTS)])


class Active(ProBusXEntity, BinarySensorEntity):
    _attr_icon = "mdi:pump"

    def __init__(self, c, i, name):
        super().__init__(c, f"{name.lower()}_aktiv", f"{name} aktiv", "main" if i < 11 else "ext")
        self.index = i

    @property
    def is_on(self):
        value = output(self.coordinator.data["outputs_raw"][self.index], self.index)
        return None if value is None else value > 0

    @property
    def extra_state_attributes(self):
        return {
            "mapping": (
                "provisional_zero_only" if self.index >= 11 else "capture_and_conexio_webcode"
            )
        }
