# BeMoS Modbus
## Einleitung
Dieser Dienst stellt einen Wrapper für Teile der BeMoS-API über Modbus TCP auf Port `502` im Netzwerk bereit. Da Modbus keinerlei Authentifizierung beherrscht, muss auf Netzwerkbasis sichergestellt werden, dass nur berechtigte Geräte Zugriff auf diesen Port am entsprechenden BeMoS-Controller haben. Aufgrund dieses Sicherheitsproblems ist der Modbus Wrapper nicht standardmäßig bei allen BeMoS Controllern aktiviert, sondern muss gesondert gestartet werden.

## Datentypen
* Daten werden **Big-Endian** geordnet übertragen
* Registerübergreifende Daten werden als **word swap** gesendet, jeweils mit der/den darauffolgenden Adresse(n) (siehe Lücken)
* Fließkommazahlen werden nach dem **IEEE 754**-Standard erzeugt
* die Adressierung ist **1-basierend**

Beispiel: `[ a b c d ] = [ c d ][ a b ]`

## Input-Register
Adressbereich 30001-39999

| Start-Adresse | Datentyp      | Messwert           | Einheit |
| ------------: | :-----------: | ------------------ | ------- |
| 3x0001        | uint32        | Unix-Zeitstempel   | s       |
| 3x0003        | float32       | Käfigdrehzahl      | RPM     |
| 3x0005        | float32       | Wellendrehzahl     | RPM     |
| 3x0007        | float32       | Temperatur         | °C      |
| 3x0009        | float32       | Störlevel          | -       |
| 3x0011        | float32       | Mittlere Laufzeit  | ns      |
| 3x0013        | float32       | Mittlere Amplitude | V       |
| 3x0015        | float32       | RMS Laufzeit       | ns      |
| 3x0017        | float32       | RMS Amplitude      | V       |
| 3x0019        | float32       | Temperatur X1      | °C      |
| 3x0021        | float32       | Temperatur X2      | °C      |
| 3x0023        | float32       | Druckwinkel        | °       |
| 3x0025        | float32       | Axialschub         | N       |

## Holding-Register
Adressbereich 40001-49999

| Start-Adresse | Datentyp      | Messwert               | Einheit |
| ------------: | :-----------: | ---------------------- | ------- |
| 4x0001        | uint16        | Externe Wellendrehzahl | RPM     |
