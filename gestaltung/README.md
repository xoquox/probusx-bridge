# Gestaltung

Das Logo besteht aus dem freigegebenen türkisen PX-Motiv ohne orangefarbene Balken.
`probusx-px-original.png` ist die unveränderte freigegebene Bilddatei.
Die transparente Fläche und der weiche Rand gehören zu dieser Fassung.

Die Firmware enthält davon abgeleitete PNG- und ICO-Dateien:

| Datei in firmware/src/assets | Zweck |
| --- | --- |
| logo.png | Seitenkopf, 192 × 192 Pixel |
| favicon.png | Browser-Symbol, 32 × 32 Pixel |
| favicon.ico | Browser-Symbol mit 16, 32, 48 und 64 Pixeln |

Die Ableitung ändert nur Dateiformat und Abmessungen. Das Seitenverhältnis bleibt erhalten.
Das Original wird nicht in voller Größe in die Firmware eingebettet.
Die Firmware liefert alle drei Dateien lokal aus ihrem Flash. Ein externer Bildserver
oder eine Verbindung ins Internet ist für die Darstellung nicht nötig.

Die Oberfläche verwendet 45-Grad-Abschrägungen statt runder Ecken. Große Flächen haben
12 Pixel, Bedienelemente 6 Pixel und Schema-Markierungen 4 Pixel Abschrägung.
Tastaturfokus und Auswahl bleiben über einen inneren Rahmen erkennbar.
Der Zuschnitt ändert weder die Bearbeitungssperre noch die gespeicherten Schema-Positionen.
Das hochgeladene Heizungsschema und das Reglerdisplay werden nicht beschnitten.

## Entstehung des Logos

Das Motiv wurde mit Bildgenerierung erstellt. Der Benutzer gab die PX-Fassung ohne
orangefarbene Balken frei. Die Aufträge stehen in `logo-auftraege.txt`.
Die erste Fassung mit Balken und spätere, nicht freigegebene Varianten sind nicht
Teil des Projekts.
