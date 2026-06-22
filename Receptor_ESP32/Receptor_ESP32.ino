/**
 * Firmware para el Receptor Principal (ESP32)
 * Recibe datos de los Emisores ESP8266 vía ESP-NOW y los envía al VPS vía HTTP POST.
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

// Dirección del servidor (VPS por defecto, o IP local de tu laptop)
const char* serverEndpoint = "http://vmi01.moondev.online/api/telemetry";
// const char* serverEndpoint = "http://192.168.43.50:3000/api/telemetry"; // Descomenta y pon la IP de tu laptop para pruebas locales

// ==========================================
// MODO AUTO-SIMULACIÓN (Para pruebas con solo el ESP32)
// ==========================================
// #define SELF_SIMULATE_MODE  // Descomenta esta línea para que el ESP32 envíe datos falsos sin los emisores 8266
unsigned long lastSimTime = 0;
int simNode = 1;

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

        // Guardar en la cola para procesamiento asíncrono en loop()
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

    // 1. Inicializar y Conectar Wi-Fi
    WiFi.mode(WIFI_AP_STA); // Modo híbrido para poder operar ESP-NOW y conectarse al Router
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
#ifdef SELF_SIMULATE_MODE
    // Generar y enviar datos simulados cada 5 segundos para pruebas sin emisores fisicos
    if (millis() - lastSimTime >= 5000) {
        lastSimTime = millis();
        TelemetryData simData;
        simData.boardId = simNode;
        simData.temp = 33.0 + (random(-15, 15) / 10.0);
        simData.hum = 60.0 + (random(-50, 50) / 10.0);
        simData.nh3 = 12.0 + (random(-40, 100) / 10.0);

        Serial.printf("🤖 [Autosimulacion ESP32] Generado Nodo %d -> Temp: %.1f | Hum: %.1f | NH3: %.1f\n", 
            simData.boardId, simData.temp, simData.hum, simData.nh3);

        if (WiFi.status() == WL_CONNECTED) {
            sendHTTPPost(simData);
        } else {
            Serial.println("❌ Autosimulacion: Wi-Fi Desconectado.");
            WiFi.begin(ssid, password);
        }

        // Rotar nodos del 1 al 4
        simNode = (simNode == 4) ? 1 : simNode + 1;
    }
#endif

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
            // Reintentar conexión Wi-Fi en segundo plano
            WiFi.begin(ssid, password);
        }
    }
    
    delay(50); // Pequeño respiro para evitar saturación del procesador
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
