# Z-Code NodeMCU Access System

Sistema de control de acceso basado en NodeMCU ESP8266 con lector RFID-RC522, pantalla OLED SSD1306, módulo relé y punto de acceso WiFi.

## Características

- **Lectura RFID**: Lee tarjetas MIFARE Classic (13.56 MHz) mediante el módulo RC522.
- **Verificación criptográfica**: Calcula `(UID_decimal × 2686358447) mod 2³²` y lo compara con el valor almacenado en el bloque 4 de la tarjeta. Solo las tarjetas con el código correcto conceden acceso.
- **Control de relé**: Activa el relé durante 10 segundos cuando el acceso es concedido.
- **Punto de acceso WiFi**: Crea una red `ZCODE-MCU-v01` con contraseña `12345678AZ` durante 1 minuto tras un acceso concedido.
- **Pantalla OLED**: Muestra el estado de la red WiFi (ON/OFF) en la línea 1 y mensajes de estado en la línea 2, con desplazamiento de texto si excede el ancho de la pantalla.
- **Bloqueo temporal**: Tras 3 intentos fallidos consecutivos, se bloquea el sistema durante 3 minutos con mensaje titilante.

## Esquema de conexiones

### RFID-RC522 → NodeMCU

| RC522    | NodeMCU | GPIO   | Función          |
|----------|---------|--------|------------------|
| SDA/SS   | D8      | GPIO15 | Selección SPI    |
| SCK      | D1      | GPIO5  | Reloj SPI        |
| MOSI     | D7      | GPIO13 | Datos → RC522    |
| MISO     | D2      | GPIO4  | Datos ← RC522    |
| RST      | D0      | GPIO16 | Reset RC522      |
| 3.3V     | 3V3     | —      | Alimentación     |
| GND      | GND     | —      | Tierra común     |

### Módulo Relé → NodeMCU

| Relé     | NodeMCU  | GPIO  | Función        |
|----------|----------|-------|----------------|
| IN/S     | D4       | GPIO2 | Señal control  |
| VCC      | VIN/5V   | —     | Alimentación   |
| GND      | GND      | —     | Tierra común   |

### OLED SSD1306 → NodeMCU (I2C)

| OLED  | NodeMCU | GPIO  | Función       |
|-------|---------|-------|---------------|
| SDA   | D2      | GPIO4 | Datos I2C     |
| SCL   | D1      | GPIO5 | Reloj I2C     |
| VCC   | 3V3     | —     | Alimentación  |
| GND   | GND     | —     | Tierra común  |

> **Nota**: MISO del RC522 y SDA del OLED comparten GPIO4 (D2).Esto funciona porque I2C y SPI no se usan simultáneamente de forma conflictiva; el I2C es direccionado y el SPI tiene línea SS dedicada.

## Requisitos

### Placa

- **NodeMCU ESP8266** (seleccionar "NodeMCU 1.0 (ESP-12E Module)" en Arduino IDE)

### Librerías (instalar desde Gestor de Librerías)

| Librería               | Autor            | Versión recomendada |
|------------------------|------------------|---------------------|
| MFRC522                | GithubCommunity  | ≥ 1.4.10           |
| Adafruit SSD1306       | Adafruit         | ≥ 2.5.0             |
| Adafruit GFX Library   | Adafruit         | ≥ 1.11.0            |

### Gestor de tarjetas

- Instalar **ESP8266** por ESP8266 Community (URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`)

## Funcionamiento

### Acceso concedido

1. Se lee la tarjeta y se verifica que `(UID_decimal × 2686358447) mod 2³²` coincida con el bloque 4.
2. Se activa el relé por **10 segundos**.
3. Se activa la red WiFi `ZCODE-MCU-v01` por **1 minuto**.
4. Línea 1 OLED: `ZCODE-MCU-v01 ON` (tamaño 2, con desplazamiento si excede ancho).
5. Línea 2 OLED: `Tarjeta OK` (tamaño 1) por 10 s → `Acercar Tarjeta` hasta completar 1 minuto.

### Acceso denegado

1. WiFi permanece deshabilitado.
2. Línea 1 OLED: `ZCODE-MCU-v01 OFF` (tamaño 2, con desplazamiento si excede ancho) durante 1 minuto.
3. Línea 2 OLED: `Tarjeta NG` (tamaño 1) por 10 s → `Acercar Tarjeta` hasta completar 1 minuto.
4. Se incrementa el contador de intentos fallidos.

### Bloqueo temporal

1. Tras **3 lecturas fallidas** consecutivas, el sistema se bloquea **3 minutos**.
2. Línea 2 OLED: `BLOQUEO TEMPORAL` titilando (400 ms ON / 400 ms OFF).
3. Línea 1 OLED: `ZCODE-MCU-v01 OFF` (tamaño 2, con desplazamiento si excede ancho).

## Preparación de tarjetas

Para que una tarjeta conceda acceso, debe tener en su **bloque 4** el valor hexadecimal resultante de:

```
código = (UID_decimal × 2686358447) mod 2³²
```

El valor se almacena como 4 bytes en formato **big-endian** en el bloque 4.

### Ejemplo de cálculo

```cpp
// UID: 0x12 0x34 0x56 0x78 → UID_decimal = 0x12345678 = 305419896
// código = (305419896 × 2686358447) mod 2³² = 0x8A3D5EF0 (ejemplo)
```

Se recomienda usar la propia función `computeVerifyCode()` del sketch para escribir el valor correcto en el bloque 4 de cada tarjeta.

## Licencia

Este proyecto es de uso privado. Todos los derechos reservados.
