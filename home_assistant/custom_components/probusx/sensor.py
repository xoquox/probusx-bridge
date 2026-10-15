"""Skaliert Rohwerte und trennt Messwerte, Stellwerte und Diagnosesensoren."""

from homeassistant.components.sensor import SensorEntity, SensorDeviceClass, SensorStateClass
from homeassistant.const import UnitOfTemperature, EntityCategory
from .const import TEMPERATURES, OUTPUTS
from .codec import temperature, output
from .entity import ProBusXEntity


async def async_setup_entry(hass, entry, async_add_entities):
    c = entry.runtime_data
    async_add_entities(
        [Temperature(c, i, name) for i, name in enumerate(TEMPERATURES)]
        + [Output(c, i, name) for i, name in enumerate(OUTPUTS)]
        + [Diagnostic(c, *spec) for spec in DIAGNOSTICS]
    )


class Temperature(ProBusXEntity, SensorEntity):
    _attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
    _attr_device_class = SensorDeviceClass.TEMPERATURE
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_suggested_display_precision = 1

    def __init__(self, c, i, name):
        super().__init__(c, name.lower(), name, "main" if i < 10 else "ext")
        self.index = i

    @property
    def native_value(self):
        return temperature(self.coordinator.data["temperatures_raw"][self.index])

    @property
    def extra_state_attributes(self):
        return {
            "raw": self.coordinator.data["temperatures_raw"][self.index],
            "validation": (
                "crc16"
                if self.family == "ext"
                or self.coordinator.data.get("diagnostics", {}).get("main_crc_verified")
                else "empirical_layout_no_crc"
            ),
        }


class Output(ProBusXEntity, SensorEntity):
    _attr_native_unit_of_measurement = "%"
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_icon = "mdi:pump"

    def __init__(self, c, i, name):
        super().__init__(c, name.lower(), name, "main" if i < 11 else "ext")
        self.index = i

    @property
    def native_value(self):
        return output(self.coordinator.data["outputs_raw"][self.index], self.index)

    @property
    def extra_state_attributes(self):
        return {
            "raw": self.coordinator.data["outputs_raw"][self.index],
            "mapping": (
                "provisional_zero_only" if self.index >= 11 else "capture_and_conexio_webcode"
            ),
            "meaning": "commanded_output_not_measured_pump_speed",
        }


# Zähler gelten seit dem Analysestart oder dem Laden der Integration. Das Attribut scope nennt den Bezug.
DIAGNOSTICS = [
    ("crc_bad", "CRC Fehler", None, "diagnostics", True),
    ("crc_ok", "CRC gültig", None, "diagnostics", True),
    ("crc_error_percent_60s", "CRC Fehlerquote 60 s", "%", "diagnostics", False),
    ("uart_errors", "UART Fehler", None, "diagnostics", True),
    ("rx_bytes_per_s", "Bus Empfangsrate", "B/s", "diagnostics", False),
    ("partial_timeouts", "Teiltelegramm Timeouts", None, "diagnostics", True),
    ("main_gaps", "Hauptdaten Unterbrechungen", None, "diagnostics", True),
    ("display_gaps", "Display Unterbrechungen", None, "diagnostics", True),
    ("state_errors", "HA Datenabruf Fehler", None, "ha_transport", True),
    ("display_errors", "HA Bildabruf Fehler", None, "ha_transport", True),
    ("http_ms", "HA Antwortzeit", "ms", "ha_transport", False),
]


class Diagnostic(ProBusXEntity, SensorEntity):
    _attr_entity_category = EntityCategory.DIAGNOSTIC
    _attr_icon = "mdi:chart-line"

    def __init__(self, c, key, name, unit, source, counter):
        super().__init__(c, f"diagnostic_{source}_{key}", name)
        self.key, self.source = key, source
        self._attr_native_unit_of_measurement = unit
        self._attr_state_class = (
            SensorStateClass.TOTAL_INCREASING if counter else SensorStateClass.MEASUREMENT
        )

    @property
    def available(self):
        return (
            super().available
            and self.key in self.coordinator.data.get(self.source, {})
            and (
                self.source != "diagnostics"
                or self.coordinator.data.get("diagnostics", {}).get("enabled", True)
            )
        )

    @property
    def native_value(self):
        value = self.coordinator.data.get(self.source, {}).get(self.key)
        return None if value is None or value < 0 else value

    @property
    def extra_state_attributes(self):
        return {
            "scope": (
                "since_integration_load"
                if self.source == "ha_transport"
                else "since_analysis_start_or_60s_window"
            )
        }
