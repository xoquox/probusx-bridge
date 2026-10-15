"""Verknüpft Entity-Kennungen und familienspezifische Frische mit dem gemeinsamen Gerät."""

from homeassistant.helpers.update_coordinator import CoordinatorEntity
from homeassistant.helpers.device_registry import DeviceInfo
from .const import DOMAIN
from .codec import fresh


class ProBusXEntity(CoordinatorEntity):
    _attr_has_entity_name = True

    def __init__(self, coordinator, key, name, family=None):
        super().__init__(coordinator)
        self._attr_unique_id = f"{coordinator.identity}_{key}"
        self._attr_name = name
        self.family = family
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, coordinator.identity)},
            name="ProBusX",
            manufacturer="Waveshare",
            model="ESP32-S3 ProBusX Bridge",
            sw_version=coordinator.data.get("version"),
            configuration_url=coordinator.url,
        )

    @property
    def available(self):
        return super().available and (
            self.family is None or fresh(self.coordinator.data, self.family)
        )
