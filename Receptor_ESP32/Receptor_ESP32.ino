/**
 * Firmware para el Receptor Principal (ESP32)
 * Recibe datos de los Emisores vía ESP-NOW y los envía al VPS vía HTTP POST.
 * 
 * Conexión Wi-Fi:
 * - SSID: OPPO Reno7
 * - Contraseña: 982778218
 * 
 * Endpoint VPS:
 * - URL: http://vmi01.moondev.online/api/telemetry
 */

#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <ArduinoJson.h> // Requiere tener instalada la librería "ArduinoJson" por Benoit Blanchon (v6 o v7)

// ==========================================
// CONFIGURACIÓN DE RED Y SERVIDOR
// ==========================================
const char* ssid = "OPPO Reno7";
const char* password = "982778218";
const char* serverEndpoint = "http://vmi01.moondev.online/api/telemetry";

// Estructura del paquete de datos (Debe coincidir con la de los Emisores)
struct __attribute__((packed)) TelemetryData {
    int boardId;
    float temp;
    float hum;
    float nh3;
};

// Cola simple para almacenar lecturas recibidas desde el callback y procesarlas en el loop principal
#define QUEUE_SIZE 10
TelemetryData readingQueue[QUEUE_SIZE];
volatile int queueHead = 0;
volatile int queueTail = 0;

// Mutex para proteger la cola en entornos multi-hilo del ESP32
portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED;

// ==========================================
// CALLBACK AL RECIBIR DATOS POR ESP-NOW
// ==========================================
void OnDataRecv(const esp_now_recv_info_t *recvInfo, const uint8_t *incomingData, int len) {
    if (len == sizeof(TelemetryData)) {
        TelemetryData tempReading;
        memcpy(&tempReading, incomingData, sizeof(tempReading));
        
        // Mostrar en serial lo que se acaba de recibir
        Serial.print("📡 ESP-NOW Recibido de MAC: ");
        for (int i = 0; i < 6; i++) {
            Serial.printf("%02X", recvInfo->src_addr[i]);
            if (i < 5) Serial.print(":");
        }
        Serial.printf(" | Nodo %d: Temp: %.1f°C | Hum: %.1f%% | NH3: %.1f ppm\n", 
            tempReading.boardId, tempReading.temp, tempReading.hum, tempReading.nh3);

        // Guardar en la cola para procesamiento en loop()
        portENTER_CRITICAL(&queueMux);
        int nextHead = (queueHead + 1) % QUEUE_SIZE;
        if (nextHead != queueTail) { // Verificar que la cola no esté llena
            readingQueue[queueHead] = tempReading;
            queueHead = nextHead;
        } else {
            Serial.println("⚠️ Cola llena, lectura descartada.");
        }
        portEXIT_CRITICAL(&queueMux);
    } else {
        Serial.println("⚠️ Tamaño de paquete ESP-NOW inválido recibido.");
    }
}

// ==========================================
// CONFIGURACIÓN INICIAL (SETUP)
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Receptor Principal ESP32 Iniciado ---");
    Serial.print("Dirección MAC del ESP32: ");
    Serial.println(WiFi.macAddress());

    // 1. Inicializar y Conectar Wi-Fi
    WiFi.mode(WIFI_AP_STA); // Modo Estación para conectar al hotspot
    WiFi.begin(ssid, password);
    
    Serial.print("Conectando al Wi-Fi (");
    Serial.print(ssid);
    Serial.print(")");
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ Wi-Fi Conectado!");
        Serial.print("Dirección IP local: ");
        Serial.println(WiFi.localIP());
        Serial.print("Canal de Wi-Fi: ");
        Serial.println(WiFi.channel());
    } else {
        Serial.println("\n❌ No se pudo conectar al Wi-Fi. Iniciando en modo offline.");
    }

    // 2. Inicializar ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Error al inicializar ESP-NOW");
        return;
    }
    Serial.println("✅ ESP-NOW Inicializado.");

    // Registrar el callback de recepción
    esp_now_register_recv_cb(OnDataRecv);
}

// ==========================================
// BUCLE PRINCIPAL (LOOP)
// ==========================================
void loop() {
    TelemetryData readingToSend;
    bool hasData = false;

    // Extraer una lectura de la cola si está disponible
    portENTER_CRITICAL(&queueMux);
    if (queueTail != queueHead) {
        readingToSend = readingQueue[queueTail];
        queueTail = (queueTail + 1) % QUEUE_SIZE;
        hasData = true;
    }
    portEXIT_CRITICAL(&queueMux);

    // Procesar envío HTTP si hay datos y Wi-Fi conectado
    if (hasData) {
        if (WiFi.status() == WL_CONNECTED) {
            sendHTTPPost(readingToSend);
        } else {
            Serial.println("❌ Intento de envío omitido: Wi-Fi Desconectado.");
            WiFi.begin(ssid, password);
        }
    }
    
    delay(50); // Pequeño respiro
}

// ==========================================
// ENVIAR LECTURAS AL SERVIDOR VPS (HTTP POST)
// ==========================================
void sendHTTPPost(TelemetryData data) {
    HTTPClient http;
    
    // Iniciar conexión al endpoint del VPS
    http.begin(serverEndpoint);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-Key", "PollitosSecureIoTKey2026");

    // Construir el documento JSON
    StaticJsonDocument<200> doc;
    doc["boardId"] = data.boardId;
    doc["temp"] = data.temp;
    doc["hum"] = data.hum;
    doc["nh3"] = data.nh3;

    String jsonString;
    serializeJson(doc, jsonString);

    Serial.print("📤 Enviando POST al servidor... ");
    int httpResponseCode = http.POST(jsonString);

    if (httpResponseCode > 0) {
        Serial.print("Código de respuesta HTTP: ");
        Serial.println(httpResponseCode);
        String response = http.getString();
        Serial.print("Respuesta: ");
        Serial.println(response);
    } else {
        Serial.print("❌ Error al enviar POST. Código de error: ");
        Serial.println(http.errorToString(httpResponseCode).c_str());
    }

    http.end(); // Cerrar conexión
}