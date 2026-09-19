# AIR CHECK – Strom, Akku, Sicherheit

Kurzanleitung für den Alltag. Technische Hintergründe: [ENGINEERING_DECISIONS.md](ENGINEERING_DECISIONS.md)
(EDR-21), [BATTERY_LIFE.md](BATTERY_LIFE.md). Vorlage: Power-Standard, Modul C.

## 1. Strom

- **Netzteil:** beliebiges USB-C-Netzteil ab 5 V / 1,5 A (z. B. das 18-W-Haushaltsnetzteil) mit
  **USB-C-zu-C-Kabel**, an die Buchse **oben** am Gerät. Kein Schnellladegerät nötig.
- **Dauerbetrieb am Kabel ist erlaubt**, auch unbeaufsichtigt und über Nacht. Ist der Akku voll, pausiert das
  Gerät das Laden selbst und lädt erst wieder nach, wenn der Akku benutzt wurde, unter 3,30 V fällt oder
  30 Tage vergangen sind.
- Kabel ziehen → das Gerät läuft ohne Unterbrechung aus dem Akku weiter.
- Die zweite USB-C-Buchse an der rechten Seite (von vorn gesehen) ist nur zum Konfigurieren und für Updates.
  Sie lädt den Akku **nicht**.

## 2. Akku

- Typ: **LiFePO4 (Lithium-Eisenphosphat) 3,2 V**, 4 × Lithium Werks **AER18650m2A2**, parallel. Keine anderen
  Zellen!
- Laufzeit ohne Kabel: etwa **2,9 Monate** im Normalbetrieb (ECO: Feinstaub stündlich, CO₂ alle 5 min),
  mit 25 % Reserve gerechnet. Häufigeres Messen verkürzt das deutlich (NORMAL etwa 4 Wochen).
- Ladezeit aus leer: etwa **9 h**. Das Gerät nimmt sich während des Ladens selbst einen Teil des Stroms. Am
  Dauerkabel wird fast nie aus leer geladen.
- **Während des Ladens** können Temperatur und Luftfeuchte etwas zu hoch angezeigt werden: Die Ladeelektronik
  wird warm. Die App bzw. das Dashboard zeigt „lädt“, solange das der Fall ist.
- Laden nur bei **0–45 °C Umgebung**. Nicht auf dem Heizkörper, nicht in praller Sonne, nicht abgedeckt.
  Bei Kälte lädt das Gerät absichtlich nicht und läuft aus dem Akku weiter.

## 3. Anzeigen (Apple Home / Matter und die LED)

| Anzeige | Bedeutung | Was tun |
|---|---|---|
| Akku „lädt“ | USB steckt, Akku wird geladen | nichts; T/RH können kurz etwas hoch sein |
| Akku „voll“ / „lädt nicht“ bei USB | voll oder Ladepause | nichts |
| **Akku schwach** (LED: gelbes Blinken auf Tastendruck) | noch etwa 10 % (3,20 V) | Kabel anstecken |
| **Akku kritisch** (LED: kurzer roter Blitz alle 10 s) | noch etwa 6 % (3,10 V), das Gerät misst nicht mehr | sofort Kabel anstecken |
| **Fehler / „Ersatz nötig“** | zu kalt/heiß beim Laden, oder Ladezeit zweimal überschritten | Gerät in normale Raumtemperatur bringen, Kabel neu stecken. Kommt der Fehler wieder: nicht mehr laden, siehe §4 |

Bei 3,0 V schaltet die Ladeelektronik das Gerät selbst ab, damit die Zellen nicht tiefentladen werden. Mit
Kabel startet es wieder.

## 4. Wenn etwas nicht stimmt – sofort handeln

**Anzeichen:** Das Gehäuse ist am Akkufach deutlich heiß, eine Zelle ist aufgebläht, verbeult oder undicht,
es riecht stechend, oder das Gerät ist heruntergefallen und das Akkufach beschädigt.

1. Kabel ziehen. **Nicht weiter laden.**
2. Wenn gefahrlos möglich, die Akkufach-Tür auf der Rückseite öffnen (zwei Schrauben, **Innensechskant 2 mm**)
   und die Zellen entnehmen. Dabei nicht mit bloßen Händen an eine heiße Zelle fassen.
3. Zellen **im Freien auf nicht brennbarer Unterlage** (Stein, Fliesen, Metallblech) ablegen, mit Abstand zu
   Brennbarem. Nicht in Wasser tauchen, nicht in den Hausmüll.
4. Nach dem Abkühlen die Pole abkleben und die Zellen zur Batterie-Sammelstelle bringen.
5. Qualm oder Flammen: Raum verlassen, Tür schließen, **Feuerwehr 112**.

## 5. Zellwechsel (nur zur Reparatur)

1. Kabel ziehen, Akkufach-Tür mit Innensechskant 2 mm öffnen.
2. **Alle vier** Zellen entnehmen. Nie einzelne Zellen tauschen.
3. Neue Zellen gleichen Typs (AER18650m2A2) und gleicher Charge, vorher **gemeinsam im XTAR MX4,
   Schalter auf „LiFePO4“**, voll laden, bis alle grün sind.
4. Spannungen messen: **höchstens 20 mV Unterschied** zwischen den Zellen.
5. Polarität wie im Fach markiert (+ / −) einsetzen, Temperaturfühler wieder in seine Halterung zwischen den
   beiden Haltern drücken, Tür zuschrauben.

## 6. Lagern, Transport, Entsorgen

- **Längere Lagerung** (> 1 Monat, Gerät aus): Akku halb voll (etwa 3,3 V je Zelle), kühl und trocken,
  10–25 °C. Alle 6 Monate kurz nachladen.
- **Transport:** Zellen nur im Gerät oder einzeln in Schutzhüllen/Boxen. Nie lose in der Tasche (Kurzschluss mit
  Schlüsseln/Münzen). Für Post- oder Luftversand gelten eigene Regeln für Lithiumzellen.
- **Entsorgen:** Zellen und Gerät **getrennt**. Zellen mit abgeklebten Polen in die Batterie-Sammelbox
  (Supermarkt, Wertstoffhof). Das Gerät ohne Zellen zum Elektroschrott (Wertstoffhof).

## 7. Nicht tun

- Keine anderen Zellen oder Akkus einsetzen, auch keine „normalen“ 3,7-V-Li-Ion-18650 und keine AA-Zellen.
- Nicht mit einem anderen Ladegerät im Gerät laden, nicht öffnen und an der Elektronik ändern.
- Gerät nicht abdecken, nicht in feuchten Räumen (Bad) betreiben, die Lüftungsschlitze frei lassen.
