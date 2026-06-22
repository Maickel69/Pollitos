/**
 * Firmware de Pruebas para el Emisor ESP32 con Sensor LM35
 * 
 * Este código es una versión de pruebas para que puedas probar tu hardware hoy mismo.
 * - Temperatura: Lee el sensor analógico LM35.
 * - Humedad y Amoníaco: Valores simulados (ya que no tienes los sensores físicos aún).
 * - Envío: ESP-NOW al Receptor ESP32.
 * 
 * Conexión del LM35 al ESP32:
 * - VCC (Pin izquierdo del LM35 plano frontal) -> Conectar a Vin (5V) o 3.3V del ESP32.
 * - Vout (Pin central del LM35) -> Conectar a GPIO 34 del ESP32 (ADC1).
 * - GND (Pin derecho del LM35 plano frontal) -> Conectar a GND del ESP32.
 */
#include <WiFi.h>
#include <esp_now.h>
// ==========================================
// CONFIGURACIÓN DEL NODO
// ==========================================
#define BOARD_ID 1  // ID del nodo emisor de pruebas
// Dirección MAC del ESP32 Receptor (Modificar con la MAC real de tu receptor)
// MAC actual del usuario: 00:70:07:7e:61:a8
uint8_t receiverAddress[] = {0x00, 0x70, 0x07, 0x7E, 0x61, 0xA8};
// Frecuencia de envío (5 segundos)
const unsigned long SEND_INTERVAL = 5000; 
// ==========================================
// CONFIGURACIÓN DE SENSORES
// ==========================================
#define LM35_PIN 19        // GPIO 34 (ADC1_CH6) para leer el LM35
// Estructura del paquete de datos
struct __attribute__((packed)) TelemetryData {
    int boardId;
    float temp;
    float hum;
    float nh3;
};
TelemetryData dataPacket;
unsigned long lastSendTime = 0;
// Variables para simular humedad y amoníaco
float simulatedHum = 58.0;
float simulatedNh3 = 6.5;
// ==========================================
// CONFIGURACIÓN INICIAL (SETUP)
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Emisor ESP32 de Pruebas (LM35) Iniciado ---");
    // Configurar resolución ADC a 12 bits (0 - 4095)
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
        Serial.print("Estado de envío ESP-NOW: ");
        Serial.println(status == ESP_NOW_SEND_SUCCESS ? "ÉXITO (Entregado al receptor)" : "FALLO (Receptor no responde)");
    });
    // Registrar Peer (Receptor)
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("❌ Error al registrar el receptor");
        return;
    }
    Serial.println("✅ Receptor registrado con éxito.");
}
// ==========================================
// BUCLE PRINCIPAL (LOOP)
// ==========================================
void loop() {
    if (millis() - lastSendTime >= SEND_INTERVAL) {
        lastSendTime = millis();
        // 1. Leer temperatura real del sensor LM35
        float adcVal = 0;
        // Tomar promedio de 20 muestras para estabilizar
        for(int i = 0; i < 20; i++) {
            adcVal += analogRead(LM35_PIN);
            delay(5);
        }
        adcVal = adcVal / 20.0;
        // Convertir ADC a Voltaje (ESP32: 3.3V ref, 12-bit resol: 4095)
        float millivolts = (adcVal / 4095.0) * 3300.0;
        
        // LM35 entrega 10mV por cada 1°C. Temp = mV / 10
        float temp = millivolts / 10.0;
        // Limitar temperatura a valores razonables en caso de falsos contactos
        if (temp < 0.0 || temp > 100.0) temp = 0.0;
        // 2. Simular Humedad y Amoníaco con caminata aleatoria
        simulatedHum += (random(-50, 50) / 100.0);
        simulatedHum = constrain(simulatedHum, 30.0, 90.0);
        simulatedNh3 += (random(-20, 20) / 100.0);
        simulatedNh3 = constrain(simulatedNh3, 0.1, 25.0);
        // 3. Empaquetar
        dataPacket.boardId = BOARD_ID;
        dataPacket.temp = temp;
        dataPacket.hum = simulatedHum;
        dataPacket.nh3 = simulatedNh3;
        Serial.print("[Nodo ");
        Serial.print(BOARD_ID);
        Serial.print("] Enviando -> Temp (LM35): "); 
        Serial.print(temp); Serial.print(" °C | Hum (Sim): ");
        Serial.print(simulatedHum); Serial.print(" % | NH3 (Sim): ");
        Serial.print(simulatedNh3); Serial.println(" ppm");
        // 4. Enviar vía ESP-NOW al receptor principal
        esp_now_send(receiverAddress, (uint8_t *) &dataPacket, sizeof(dataPacket));
    }
}
