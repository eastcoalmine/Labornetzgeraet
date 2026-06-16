# Netzgerät

Ein Netzgerät welches 15V liefert, mit 3.3V, 5V, 12V und 15V Ausgängen versehen.

Sowie einen Shunt Widerstand welcher den Gesamtstromverbrauch misst und auf ein Display überträgt.

Logik mit Strommessung über Shunt Widerstand & Display. Wird von einem Raspberry Pi Pico (C/C++ SDK) gesteuert. Zusätzlich noch 5V Lüfter als kühlung.
Logik und Lüfter verbraucht 0.25 A bei 5V im Ruhezustand.

<img width="1008" height="450" alt="image" src="https://github.com/user-attachments/assets/472d8deb-96c7-4b97-a055-1cd9b8b240ed" />



---

## Material

Foglendes Material wurde verbaut:

- Raspberry Pi Pico
- ADC (ADS1115)
- Display (Waveshare 1602 I2C)
- Lüfter
- Netzgerät (15V / 10 A)
- Lineare Spannungsregler (LM2937, LM7805, MC7812)
- Operationsverstärker (MCP601)
- Widerstände
- Kondensatoren

---

## Shunt Widerstand

Dieser ist auf dem GND Pfad montiert (über alle Spannungsausgänge), da bei der Montage beim VCC der Vin+ und Vin- Pin beim op amp höher sind als VCC. 

---




