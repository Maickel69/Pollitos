/**
 * Firmware para los Nodos Emisores (ESP32)
 * Sensor de Temperatura y Humedad: GY-SHT31-D (I2C)
 * Sensor de Amoníaco (NH3): MQ-137 (Analógico en pin GPIO 34 / ADC1)
 * Comunicación: ESP-NOW a Receptor ESP32
 * 
 * INSTRUCCIONES:
 * 1. Para cada emisor, cambia el valor de BOARD_ID (1, 2, 3 o 4) antes de subir.
 * 2. Coloca la dirección MAC del ESP32 Receptor principal en la constante `receiverAddress`.
 * 3. Asegúrate de tener instalada la librería "Adafruit SHT31" en el Arduino IDE.
 */
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
// ==========================================
// CONFIGURACIÓN DEL NODO
// ==========================================
#define BOARD_ID 1  // Cambiar a 1, 2, 3 o 4 según corresponda
// Dirección MAC del ESP32 Receptor (Modificar con la MAC real de tu receptor)
// MAC actual especificada por el usuario: 00:70:07:7e:61:a8
uint8_t receiverAddress[] = {0x00, 0x70, 0x07, 0x7E, 0x61, 0xA8};
// Frecuencia de envío (5 segundos)
const unsigned long SEND_INTERVAL = 5000; 
// ==========================================
// CONFIGURACIÓN DE SENSORES
// ==========================================
Adafruit_SHT31 sht31 = Adafruit_SHT31();
// Parámetros del sensor MQ-137
#define MQ_PIN 34          // GPIO 34 (ADC1_CH6) en el ESP32 (¡Usar ADC1, ya que ADC2 no funciona con Wi-Fi activo!)
#define RL_VAL 1.0         // Resistencia de carga en kOhms
#define RO_CLEAN_AIR_FACTOR 3.6 // Relación Rs/Ro en aire limpio para MQ-137
// Coeficientes de la curva logarítmica para MQ-137 (Amoníaco NH3)
const float MQ_SLOPE = -0.42;
const float MQ_INTERCEPT = 0.58;
float Ro = 10.0; // Valor de calibración por defecto
// Estructura del paquete de datos (Debe coincidir con la del Receptor)
struct __attribute__((packed)) TelemetryData {
    int boardId;
    float temp;
    float hum;
    float nh3;
};
TelemetryData dataPacket;
unsigned long lastSendTime = 0;
// ==========================================
// FUNCIONES DE CALIBRACIÓN DEL MQ-137
// ==========================================
// Leer promedio de lecturas ADC (ESP32 tiene 12-bit ADC: 0 - 4095)
float readADC_Average(int pin, int samples = 50) {
    float sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += analogRead(pin);
        delay(10);
    }
    return sum / samples;
}
// Calcular la resistencia del sensor (Rs) a partir del ADC
float calculateRs(float adcVal) {
    if (adcVal <= 0) return 9999.0;
    // Fórmula para el ADC de 12 bits de ESP32 (0 - 4095)
    return (float)RL_VAL * (4095.0 - adcVal) / adcVal;
}
// Calibrar Ro en aire limpio
float calibrateRo() {
    Serial.println("Calibrando MQ-137 en aire limpio...");
    float adcAverage = readADC_Average(MQ_PIN, 100);
    float Rs = calculateRs(adcAverage);
    float calculatedRo = Rs / RO_CLEAN_AIR_FACTOR;
    Serial.print("Calibración completada. Ro obtenido: ");
    Serial.print(calculatedRo);
    Serial.println(" kOhms");
    return calculatedRo;
}
// Calcular ppm de NH3
float getNH3_ppm(float Rs) {
    float ratio = Rs / Ro;
    float log10_ppm = MQ_SLOPE * log10(ratio) + MQ_INTERCEPT;
    return pow(10, log10_ppm);
}
// ==========================================
// CONFIGURACIÓN INICIAL (SETUP)
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.print("--- Iniciando Emisor ESP32 - Nodo ");
    Serial.print(BOARD_ID);
    Serial.println(" ---");
    // Inicializar I2C para el SHT31 (Default en ESP32: SDA=GPIO 21, SCL=GPIO 22)
    Wire.begin();
    if (!sht31.begin(0x44)) {
        Serial.println("❌ No se encontró el sensor SHT31. Verifica conexiones SDA/SCL.");
    } else {
        Serial.println("✅ Sensor SHT31 inicializado.");
    }
    // Configurar resolución ADC a 12 bits (rango 0-4095)
    analogReadResolution(12);
    // Inicializar Wi-Fi en modo Estación (Requerido para ESP-NOW)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // Inicializar ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Error al inicializar ESP-NOW");
        return;
    }
    Serial.println("✅ ESP-NOW inicializado.");
    // Registrar callback de envío (compatible con ESP32 Core v2.x y v3.x)
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    esp_now_register_send_cb([](const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
#else
    esp_now_register_send_cb([](const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
        Serial.print("Estado de envío: ");
        Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Éxito" : "Fallo");
    });
    // Registrar Peer (Receptor)
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("❌ Error al registrar el peer (Receptor)");
        return;
    }
    Serial.println("✅ Peer (Receptor) registrado con éxito.");
    // Calibración
    delay(2000);
    Ro = calibrateRo();
}
// ==========================================
// BUCLE PRINCIPAL (LOOP)
// ==========================================
void loop() {
    if (millis() - lastSendTime >= SEND_INTERVAL) {
        lastSendTime = millis();
        // 1. Leer SHT31
        float temp = sht31.readTemperature();
        float hum = sht31.readHumidity();
        if (isnan(temp) || isnan(hum)) {
            Serial.println("⚠️ Error al leer SHT31. Usando valores por defecto.");
            temp = 0.0;
            hum = 0.0;
        }
        // 2. Leer MQ-137
        float adcVal = readADC_Average(MQ_PIN, 10);
        float Rs = calculateRs(adcVal);
        float nh3 = getNH3_ppm(Rs);
        if (isnan(nh3) || nh3 < 0.0) nh3 = 0.0;
        // 3. Preparar paquete
        dataPacket.boardId = BOARD_ID;
        dataPacket.temp = temp;
        dataPacket.hum = hum;
        dataPacket.nh3 = nh3;
        Serial.print("[Nodo ");
        Serial.print(BOARD_ID);
        Serial.print("] Enviando -> ");
        Serial.printf("Temp: %.1f °C | Hum: %.1f %% | NH3: %.1f ppm\n", temp, hum, nh3);
        // 4. Enviar datos
        esp_now_send(receiverAddress, (uint8_t *) &dataPacket, sizeof(dataPacket));
    }
}
