/*
 * Z-Code NodeMCU Access System
 * =============================
 * Sistema de control de acceso con RFID-RC522, OLED SSD1306,
 * relay y punto de acceso WiFi en NodeMCU ESP8266.
 *
 * Conexiones:
 *   RFID-RC522    NodeMCU    GPIO     Función
 *   SDA/SS        D8         GPIO15   Selección SPI
 *   SCK           D1         GPIO5    Reloj SPI (software)
 *   MOSI          D7         GPIO13   Datos hacia RC522
 *   MISO          D2         GPIO4    Datos desde RC522
 *   RST           D0         GPIO16   Reset del RC522
 *   3.3V          3V3        -        Alimentación
 *   GND           GND        -        Tierra común
 *
 *   Módulo-Relé   NodeMCU    GPIO     Función
 *   IN/S          D4         GPIO2    Señal de control
 *   VCC           VIN/5V     -        Alimentación
 *   GND           GND        -        Tierra común
 *
 * Librerías requeridas (instalar desde Gestor de Librerías):
 *   - MFRC522 (by GithubCommunity)
 *   - Adafruit SSD1306 (by Adafruit)
 *   - Adafruit GFX Library (by Adafruit)
 */

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
// Configuración de pines
// ============================================================
#define PIN_RFID_SS    15   // GPIO15 (D8)  - SDA/SS del RC522
#define PIN_RFID_RST   16   // GPIO16 (D0)  - RST del RC522
#define PIN_RELAY       2   // GPIO2  (D4)  - Control del relé

// ============================================================
// Configuración OLED SSD1306
// ============================================================
#define OLED_WIDTH     128
#define OLED_HEIGHT     64
#define OLED_ADDR     0x3C
#define OLED_RST       -1   // Reset compartido con el MCU

// ============================================================
// Configuración WiFi AP
// ============================================================
const char AP_SSID[] = "ZCODE-MCU-v01";
const char AP_PASS[] = "12345678AZ";

// ============================================================
// Constantes de tiempo (milisegundos)
// ============================================================
#define RELAY_ACTIVE_MS        10000UL  // 10 segundos de relé activo
#define WIFI_ACTIVE_MS         60000UL  // 1 minuto de WiFi AP activo
#define STATUS_DISPLAY_MS      10000UL  // 10 segundos mostrando OK/NG
#define BLOCK_DURATION_MS     180000UL  // 3 minutos de bloqueo temporal
#define BLINK_PERIOD_MS         400UL   // 400 ms titilo ON/OFF
#define SCROLL_INTERVAL_MS       80UL   // Velocidad desplazamiento (3 ≈ 80ms)
#define SCROLL_STEP_PIXELS         2     // Píxeles de avance por frame

// ============================================================
// Bloque de la tarjeta donde se guarda el código de verificación
// ============================================================
#define VERIFY_BLOCK  4

// ============================================================
// Constante para cálculo de verificación
// ============================================================
#define VERIFY_KEY    2686358447ULL

// ============================================================
// Variables globales
// ============================================================
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RST);
MFRC522 mfrc522(PIN_RFID_SS, PIN_RFID_RST);
MFRC522::MIFARE_Key key;

// --- Máquina de estados del sistema ---
enum SystemState {
  STATE_IDLE,              // Esperando tarjeta
  STATE_GRANTED,          // Acceso concedido
  STATE_DENIED,            // Acceso denegado
  STATE_TEMP_BLOCKED       // Bloqueo temporal activo
};

SystemState sysState = STATE_IDLE;

// --- Temporizadores ---
unsigned long stateEnterTime     = 0;   // Momento en que se entró al estado actual
unsigned long statusDisplayStart = 0;   // Inicio de muestra de "Tarjeta OK/NG"
unsigned long blinkLastToggle    = 0;   // Último cambio de titilo

// --- Flags ---
bool relayActive       = false;
bool wifiActive        = false;
bool statusPhaseOver   = false;  // true cuando pasaron los 10 s de "Tarjeta OK/NG"
bool blinkVisible      = true;

// --- Contadores ---
uint8_t failedCount    = 0;

// --- Control de desplazamiento de texto línea 1 ---
int16_t  scrollOffset    = 0;
unsigned long scrollLast = 0;
uint16_t line1PixelWidth = 0;
bool     needsScrolling  = false;
char     line1Text[32];              // Texto actual de la línea 1
char     line2Text[24];              // Texto actual de la línea 2

// ============================================================
// Prototipos de funciones
// ============================================================
void initOLED();
void initRFID();
void showIdleScreen();
void checkRFID();
void handleGranted();
void handleDenied();
void handleTempBlock();
void activateRelay();
void deactivateRelay();
void activateWiFiAP();
void deactivateWiFiAP();
void renderLine1(const char* text, bool resetScroll);
void updateScrolling();
uint32_t computeVerifyCode(uint32_t uidDecimal);
uint32_t readBlock4(MFRC522::Uid uid);
uint32_t uidToDecimal(MFRC522::Uid uid);
void setLine2(const char* text);
void refreshDisplay();

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("Z-Code NodeMCU Access System v1.0"));

  // Pin del relé
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  // Inicializar periféricos
  initOLED();
  initRFID();

  // Clave por defecto MIFARE Classic
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  // Pantalla inicial
  showIdleScreen();
  Serial.println(F("Sistema listo. Acerque tarjeta..."));
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  switch (sysState) {
    case STATE_IDLE:
      checkRFID();
      break;
    case STATE_GRANTED:
      handleGranted();
      break;
    case STATE_DENIED:
      handleDenied();
      break;
    case STATE_TEMP_BLOCKED:
      handleTempBlock();
      break;
  }
}

// ============================================================
// Inicialización del OLED SSD1306
// ============================================================
void initOLED() {
  Wire.begin();  // SDA=GPIO4(D2), SCL=GPIO5(D1)
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("Error: OLED no detectado"));
    while (true);
  }
  display.clearDisplay();
  display.display();
  Serial.println(F("OLED SSD1306 inicializado"));
}

// ============================================================
// Inicialización del lector RFID-RC522
// ============================================================
void initRFID() {
  SPI.begin();
  mfrc522.PCD_Init();
  delay(4);
  byte ver = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("RC522 Version: 0x"));
  Serial.println(ver, HEX);
  if (ver == 0x00 || ver == 0xFF) {
    Serial.println(F("Warning: RC522 no detectado"));
  }
}

// ============================================================
// Pantalla de estado IDLE
// ============================================================
void showIdleScreen() {
  display.clearDisplay();
  renderLine1(AP_SSID " OFF", true);
  setLine2("Acercar Tarjeta");
  refreshDisplay();
}

// ============================================================
// Establecer texto de línea 2 (tamaño 1, centrado en y=16)
// ============================================================
void setLine2(const char* text) {
  strncpy(line2Text, text, sizeof(line2Text) - 1);
  line2Text[sizeof(line2Text) - 1] = '\0';
}

// ============================================================
// Refrescar pantalla completa (línea 1 + línea 2)
// ============================================================
void refreshDisplay() {
  display.clearDisplay();

  // --- Línea 1 ---
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  if (!needsScrolling) {
    // Centrar texto
    uint16_t w1, h1;
    int16_t x1, y1;
    display.getTextBounds(line1Text, 0, 0, &x1, &y1, &w1, &h1);
    display.setCursor((OLED_WIDTH - w1) / 2, 0);
    display.print(line1Text);
  } else {
    display.setCursor(-scrollOffset, 0);
    display.print(line1Text);
  }

  // --- Línea 2 ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  uint16_t w2, h2;
  int16_t x2, y2;
  display.getTextBounds(line2Text, 0, 0, &x2, &y2, &w2, &h2);
  display.setCursor((OLED_WIDTH - w2) / 2, 16);
  display.print(line2Text);

  display.display();
}

// ============================================================
// Configurar línea 1 y reiniciar desplazamiento si es necesario
// ============================================================
void renderLine1(const char* text, bool resetScroll) {
  strncpy(line1Text, text, sizeof(line1Text) - 1);
  line1Text[sizeof(line1Text) - 1] = '\0';

  if (resetScroll) {
    scrollOffset = 0;
    scrollLast = millis();
  }

  // Calcular ancho del texto
  display.setTextSize(2);
  display.setTextWrap(false);
  uint16_t w, h;
  int16_t x, y;
  display.getTextBounds(line1Text, 0, 0, &x, &y, &w, &h);
  line1PixelWidth = w;
  needsScrolling = (w > OLED_WIDTH);
}

// ============================================================
// Actualizar desplazamiento de línea 1
// ============================================================
void updateScrolling() {
  if (!needsScrolling) return;

  unsigned long now = millis();
  if (now - scrollLast < SCROLL_INTERVAL_MS) return;
  scrollLast = now;

  scrollOffset += SCROLL_STEP_PIXELS;

  // Cuando el texto se ha desplazado completamente, reiniciar
  if (scrollOffset >= (int16_t)(line1PixelWidth)) {
    scrollOffset = 0;
  }

  refreshDisplay();
}

// ============================================================
// Verificar presencia de tarjeta RFID y procesar
// ============================================================
void checkRFID() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  Serial.print(F("UID: "));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();

  // Leer bloque 4
  uint32_t storedValue = readBlock4(mfrc522.uid);

  // Calcular valor esperado
  uint32_t uidDec = uidToDecimal(mfrc522.uid);
  uint32_t expected = computeVerifyCode(uidDec);

  Serial.print(F("  UID decimal: ")); Serial.println(uidDec);
  Serial.print(F("  Esperado: 0x")); Serial.println(expected, HEX);
  Serial.print(F("  Bloque 4: 0x")); Serial.println(storedValue, HEX);

  // Detener comunicación con tarjeta
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  if (storedValue == expected) {
    // ---- ACCESO CONCEDIDO ----
    Serial.println(F(">>> ACCESO CONCEDIDO"));
    failedCount = 0;
    sysState = STATE_GRANTED;
    stateEnterTime = millis();

    activateRelay();
    relayActive = true;

    activateWiFiAP();
    wifiActive = true;

    renderLine1(AP_SSID " ON", true);
    setLine2("Tarjeta OK");
    statusDisplayStart = millis();
    statusPhaseOver = false;
    refreshDisplay();

  } else {
    // ---- ACCESO DENEGADO ----
    Serial.println(F(">>> ACCESO DENEGADO"));
    failedCount++;
    Serial.print(F("  Fallos consecutivos: ")); Serial.println(failedCount);

    if (failedCount >= 3) {
      // ---- BLOQUEO TEMPORAL ----
      Serial.println(F(">>> BLOQUEO TEMPORAL (3 min)"));
      failedCount = 0;
      sysState = STATE_TEMP_BLOCKED;
      stateEnterTime = millis();
      blinkLastToggle = millis();
      blinkVisible = true;

      renderLine1(AP_SSID " OFF", true);
      setLine2("BLOQUEO TEMPORAL");
      refreshDisplay();
      return;
    }

    sysState = STATE_DENIED;
    stateEnterTime = millis();

    // WiFi permanece deshabilitado
    renderLine1(AP_SSID " OFF", true);
    setLine2("Tarjeta NG");
    statusDisplayStart = millis();
    statusPhaseOver = false;
    refreshDisplay();
  }
}

// ============================================================
// Manejar ACCESO CONCEDIDO
// ============================================================
void handleGranted() {
  unsigned long now = millis();
  unsigned long elapsed = now - stateEnterTime;

  // --- Relé: 10 segundos ---
  if (relayActive && (now - stateEnterTime >= RELAY_ACTIVE_MS)) {
    deactivateRelay();
    relayActive = false;
  }

  // --- Línea 2: "Tarjeta OK" por 10 s ---
  if (!statusPhaseOver && (now - statusDisplayStart >= STATUS_DISPLAY_MS)) {
    statusPhaseOver = true;
    setLine2("Acercar Tarjeta");
    refreshDisplay();
  }

  // --- WiFi AP: 1 minuto ---
  if (wifiActive && (elapsed >= WIFI_ACTIVE_MS)) {
    deactivateWiFiAP();
    wifiActive = false;
    sysState = STATE_IDLE;
    showIdleScreen();
    return;
  }

  // --- Desplazamiento línea 1 ---
  updateScrolling();
}

// ============================================================
// Manejar ACCESO DENEGADO
// ============================================================
void handleDenied() {
  unsigned long now = millis();
  unsigned long elapsed = now - stateEnterTime;

  // --- Línea 2: "Tarjeta NG" por 10 s ---
  if (!statusPhaseOver && (now - statusDisplayStart >= STATUS_DISPLAY_MS)) {
    statusPhaseOver = true;
    setLine2("Acercar Tarjeta");
    refreshDisplay();
  }

  // --- Después de 1 minuto: volver a IDLE ---
  if (elapsed >= WIFI_ACTIVE_MS) {
    sysState = STATE_IDLE;
    showIdleScreen();
    return;
  }

  // --- Desplazamiento línea 1 ---
  updateScrolling();
}

// ============================================================
// Manejar BLOQUEO TEMPORAL (3 minutos)
// ============================================================
void handleTempBlock() {
  unsigned long now = millis();
  unsigned long elapsed = now - stateEnterTime;

  // --- ¿Bloqueo expirado? ---
  if (elapsed >= BLOCK_DURATION_MS) {
    Serial.println(F("Bloqueo temporal finalizado"));
    sysState = STATE_IDLE;
    showIdleScreen();
    return;
  }

  // --- Titilar "BLOQUEO TEMPORAL" cada 400 ms ---
  if (now - blinkLastToggle >= BLINK_PERIOD_MS) {
    blinkLastToggle = now;
    blinkVisible = !blinkVisible;

    if (blinkVisible) {
      setLine2("BLOQUEO TEMPORAL");
    } else {
      setLine2("");  // Línea 2 vacía = efecto titilo OFF
    }
    refreshDisplay();
  }

  // --- Desplazamiento línea 1 ---
  updateScrolling();
}

// ============================================================
// Convertir UID a decimal (4 bytes big-endian -> uint32_t)
// ============================================================
uint32_t uidToDecimal(MFRC522::Uid uid) {
  uint32_t result = 0;
  for (byte i = 0; i < uid.size && i < 4; i++) {
    result = (result << 8) | uid.uidByte[i];
  }
  return result;
}

// ============================================================
// Calcular código de verificación: (UID_decimal * 2686358447) mod 2^32
// ============================================================
uint32_t computeVerifyCode(uint32_t uidDecimal) {
  uint64_t product = (uint64_t)uidDecimal * VERIFY_KEY;
  return (uint32_t)(product & 0xFFFFFFFFULL);
}

// ============================================================
// Leer bloque 4 de la tarjeta MIFARE Classic
// Devuelve los 4 primeros bytes como uint32_t (big-endian)
// o 0xFFFFFFFF si falla la autenticación o lectura
// ============================================================
uint32_t readBlock4(MFRC522::Uid uid) {
  MFRC522::StatusCode status;
  byte buffer[18];
  byte size = sizeof(buffer);

  // Autenticar sector 1 (bloque 4) con clave A
  status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, VERIFY_BLOCK, &key, &uid
  );
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("  Auth fail: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return 0xFFFFFFFF;
  }

  // Leer bloque 4
  status = (MFRC522::StatusCode)mfrc522.MIFARE_Read(VERIFY_BLOCK, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("  Read fail: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return 0xFFFFFFFF;
  }

  // Convertir 4 primeros bytes a uint32_t big-endian
  uint32_t value = 0;
  for (byte i = 0; i < 4; i++) {
    value = (value << 8) | buffer[i];
  }
  return value;
}

// ============================================================
// Control del relé
// ============================================================
void activateRelay() {
  digitalWrite(PIN_RELAY, HIGH);
  Serial.println(F("Relé ON"));
}

void deactivateRelay() {
  digitalWrite(PIN_RELAY, LOW);
  Serial.println(F("Relé OFF"));
}

// ============================================================
// Control del punto de acceso WiFi
// ============================================================
void activateWiFiAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print(F("WiFi AP: "));
  Serial.println(AP_SSID);
  Serial.print(F("  IP: "));
  Serial.println(WiFi.softAPIP());
}

void deactivateWiFiAP() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println(F("WiFi AP OFF"));
}
