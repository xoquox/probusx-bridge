"""Liefert das aktuelle PNG aus dem gemeinsamen Bildcache."""

from homeassistant.components.camera import Camera
from .entity import ProBusXEntity


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([Display(entry.runtime_data)])


class Display(ProBusXEntity, Camera):
    _attr_frame_interval = 0.25
    _attr_brand = "ProBusX"
    _attr_model = "Reglerdisplay 160×128"

    def __init__(self, coordinator):
        Camera.__init__(self)
        ProBusXEntity.__init__(self, coordinator, "display", "Display", "display")
        self.content_type = "image/png"

    @property
    def available(self):
        return (
            super().available
            and self.coordinator.png is not None
            and self.hass.loop.time() - self.coordinator.last_image_success
            < max(3, self.coordinator.display_ms / 1000 + 2)
        )

    @property
    def extra_state_attributes(self):
        return {"refresh_interval_ms": self.coordinator.display_ms}

    async def async_camera_image(self, width=None, height=None):
        if self.coordinator.last_update_success:
            await self.coordinator.async_refresh_image()
        return self.coordinator.png if self.available else None
