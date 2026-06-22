/**
 * Firmware para los Nodos Emisores (ESP8266)
 * Sensor de Temperatura y Humedad: GY-SHT31-D (I2C)
 * Sensor de Amoníaco (NH3): MQ-137 (Analógico en pin A0)
 * Comunicación: ESP-NOW a Receptor ESP32
 * 
 * INSTRUCCIONES:
 * 1. Para cada una de las 4 placas ESP8266, cambia el valor de BOARD_ID (1, 2, 3 o 4) antes de subir.
 * 2. Coloca la dirección MAC del ESP32 Receptor principal en la constante `receiverAddress`.
 * 3. Asegúrate de tener instalada la librería "Adafruit SHT31" en el Arduino IDE.
 */

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

// ==========================================
// CONFIGURACIÓN DEL NODO
// ==========================================
#define BOARD_ID 1  // Cambiar a 1, 2, 3 o 4 según corresponda

// Dirección MAC del ESP32 Receptor (Modificar con la MAC real de tu receptor)
// Ejemplo actual: 00:70:07:7e:61:a8 (debe especificarse en bytes)
uint8_t receiverAddress[] = {0x00, 0x70, 0x07, 0x7E, 0x61, 0xA8};

// Frecuencia de envío (5 segundos para requerimiento de alerta inmediata)
const unsigned long SEND_INTERVAL = 5000; 

// ==========================================
// CONFIGURACIÓN DE SENSORES
// ==========================================
Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Parámetros del sensor MQ-137
#define MQ_PIN A0          // Pin analógico A0 en el ESP8266
#define RL_VAL 1.0         // Resistencia de carga (Load Resistor) en el módulo en kOhms (normalmente 1.0k o 2.0k)
#define RO_CLEAN_AIR_FACTOR 3.6 // Relación Rs/Ro en aire limpio para MQ-137

// Coeficientes de la curva logarítmica para MQ-137 (Amoníaco NH3)
// Obtenidos de la curva del datasheet: log(ppm) = slope * log(Rs/Ro) + intercept
const float MQ_SLOPE = -0.42;
const float MQ_INTERCEPT = 0.58;

float Ro = 10.0; // Valor de calibración por defecto (en kOhms)

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

// Leer el valor promedio del ADC para estabilidad
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
    if (adcVal <= 0) return 999.0; // Evitar división por cero
    // Fórmula del divisor de tensión en ESP8266 (ADC de 10 bits: 0 - 1023)
    return (float)RL_VAL * (1023.0 - adcVal) / adcVal;
}

// Rutina de calibración en aire limpio para obtener Ro
float calibrateRo() {
    Serial.println("Calibrando MQ-137 en aire limpio...");
    float adcAverage = readADC_Average(MQ_PIN, 100);
    float Rs = calculateRs(adcAverage);
    // En aire limpio, Rs/Ro = RO_CLEAN_AIR_FACTOR, por lo tanto Ro = Rs / factor
    float calculatedRo = Rs / RO_CLEAN_AIR_FACTOR;
    Serial.print("Calibración completada. Ro obtenido: ");
    Serial.print(calculatedRo);
    Serial.println(" kOhms");
    return calculatedRo;
}

// Convertir la resistencia Rs actual a partes por millón (ppm) de Amoníaco
float getNH3_ppm(float Rs) {
    float ratio = Rs / Ro;
    // Ecuación: ppm = 10^( slope * log10(ratio) + intercept )
    float log10_ppm = MQ_SLOPE * log10(ratio) + MQ_INTERCEPT;
    return pow(10, log10_ppm);
}

// ==========================================
// CONFIGURACIÓN INICIAL (SETUP)
// ==========================================
void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.print("--- Iniciando Emisor ESP8266 - Nodo ");
    Serial.print(BOARD_ID);
    Serial.println(" ---");

    // Inicializar sensor SHT31 por I2C (pines por defecto en ESP8266: D2=SDA, D1=SCL)
    if (!sht31.begin(0x44)) { // Dirección I2C común para SHT31 es 0x44 o 0x45
        Serial.println("❌ No se encontró el sensor SHT31. Verifica las conexiones.");
    } else {
        Serial.println("✅ Sensor SHT31 inicializado correctamente.");
    }

    // Inicializar Wi-Fi en modo Estación (requerido para ESP-NOW)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Inicializar ESP-NOW
    if (esp_now_init() != 0) {
        Serial.println("❌ Error al inicializar ESP-NOW");
        return;
    }
    Serial.println("✅ ESP-NOW inicializado.");

    // Configurar rol del dispositivo como emisor
    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);

    // Registrar receptor
    esp_now_add_peer(receiverAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

    // Callback al enviar
    esp_now_register_send_cb([](uint8_t* mac_addr, uint8_t status) {
        Serial.print("Estado de envío: ");
        if (status == 0) {
            Serial.println("Éxito (Entregado)");
        } else {
            Serial.println("Fallo (No entregado)");
        }
    });

    // Calibrar el sensor de gas al encender (asegurar aire limpio al iniciar)
    // O puedes usar el valor de Ro fijo de 10.0k cargado por defecto si no puedes garantizar aire limpio.
    delay(2000); // Dar tiempo de precalentamiento básico
    Ro = calibrateRo();
}

// ==========================================
// BUCLE PRINCIPAL (LOOP)
// ==========================================
void loop() {
    if (millis() - lastSendTime >= SEND_INTERVAL) {
        lastSendTime = millis();

        // 1. Leer temperatura y humedad del SHT31
        float temp = sht31.readTemperature();
        float hum = sht31.readHumidity();

        // Validar lecturas del SHT31 (retorna NaN si falla)
        if (isnan(temp) || isnan(hum)) {
            Serial.println("⚠️ Error al leer SHT31. Usando valores por defecto.");
            temp = 0.0;
            hum = 0.0;
        }

        // 2. Leer Amoníaco del MQ-137
        float adcVal = readADC_Average(MQ_PIN, 10);
        float Rs = calculateRs(adcVal);
        float nh3 = getNH3_ppm(Rs);

        // Prevenir valores negativos o incoherentes
        if (isnan(nh3) || nh3 < 0.0) nh3 = 0.0;

        // 3. Preparar el paquete de datos
        dataPacket.boardId = BOARD_ID;
        dataPacket.temp = temp;
        dataPacket.hum = hum;
        dataPacket.nh3 = nh3;

        // Mostrar por consola serie
        Serial.print("[Nodo ");
        Serial.print(BOARD_ID);
        Serial.print("] Lecturas -> ");
        Serial.print("Temp: "); Serial.print(temp); Serial.print(" °C | ");
        Serial.print("Hum: "); Serial.print(hum); Serial.print(" % | ");
        Serial.print("NH3: "); Serial.print(nh3); Serial.println(" ppm");

        // 4. Enviar vía ESP-NOW al receptor principal
        esp_now_send(receiverAddress, (uint8_t *) &dataPacket, sizeof(dataPacket));
    }
}
