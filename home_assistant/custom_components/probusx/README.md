# ProBusX über HTTP

Diese benutzerdefinierte Home-Assistant-Integration wird nur für den Betrieb ohne MQTT benötigt.
Sie fragt Messwerte und Display direkt über HTTP ab und sendet Tastendrücke an die Bridge.
Ein MQTT-Broker ist dafür nicht erforderlich.
Die Komponente muss manuell installiert und aktualisiert werden.

Bei MQTT die native MQTT-Integration von Home Assistant verwenden.
Die Bridge meldet ihre Entities automatisch über Discovery an.
Diese HTTP-Komponente dann nicht zusätzlich installieren oder betreiben.
Die Bedienfeld-Karte unter `home_assistant/www/probusx` ist mit beiden Wegen nutzbar.

Einrichtung und Funktionsvergleich stehen unter [Home Assistant](../../../docs/Home-Assistant.md).
