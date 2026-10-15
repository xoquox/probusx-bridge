"""Definiert die stabilen Entity-Namen und Tastencodes der HA-Schnittstelle."""

DOMAIN = "probusx"
TEMPERATURES = [f"S{i}" for i in range(1, 11)] + [f"F1S{i}" for i in range(1, 5)]
OUTPUTS = [f"R{i}" for i in range(1, 8)] + [
    "R0",
    "HE1",
    "HE2",
    "HE3",
    "F1R1",
    "F1R2",
    "F1R3",
    "F1HE",
]
KEYS = {
    "up": "Hoch",
    "right": "Rechts",
    "down": "Runter",
    "left": "Links",
    "special": "Dreitastendruck",
}
