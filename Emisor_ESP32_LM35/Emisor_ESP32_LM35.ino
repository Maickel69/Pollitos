/**
 * Firmware de Pruebas para el Emisor ESP32 con Sensor LM35
 * 
 * - Temperatura: Lee el sensor analógico LM35.
 * - Humedad y Amoníaco: Valores simulados.
 * - Envío: ESP-NOW al Receptor ESP32.
 * 
 * Conexión del LM35 al ESP32:
 * - VCC -> Conectar a Vin (5V) o 3.3V del ESP32.
 * - Vout (Pin central) -> Conectar a un pin analógico ADC1, por ejemplo: GPIO 34 (Recomendado).
 * - GND -> Conectar a GND del ESP32.
 * 
 * NOTA DE SEGURIDAD DE PINES ESP32:
 * - El pin 19 es puramente digital y no tiene ADC (Conversor Analógico-Digital). No se puede usar para el LM35.
 * - Debes usar un pin del bloque ADC1: GPIO 32, 33, 34, 35, 36 o 39.
 * - No uses los del bloque ADC2 (como GPIO 4, 12, 14, 15, 25, 26, 27) porque se desactivan cuando se usa ESP-NOW/Wi-Fi.
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
#define LM35_PIN 34        // ¡Usa un pin analógico ADC1 como el GPIO 34! (No uses el pin 19)

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
        Serial.println(status == ESP_NOW_SEND_SUCCESS ? "ÉXITO (Entregado)" : "FALLO");
    });

    // Registrar Peer (Receptor)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.ifidx = WIFI_IF_STA;
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
        for(int i = 0; i < 20; i++) {
            adcVal += analogRead(LM35_PIN);
            delay(5);
        }
        adcVal = adcVal / 20.0;

        // Convertir ADC a Voltaje (ESP32: 3.3V ref, 12-bit resol: 4095)
        float millivolts = (adcVal / 4095.0) * 3300.0;
        
        // LM35 entrega 10mV por cada 1°C. Temp = mV / 10
        float temp = millivolts / 10.0;

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

        // Enviar datos
        esp_now_send(receiverAddress, (uint8_t *) &dataPacket, sizeof(dataPacket));
    }
}
