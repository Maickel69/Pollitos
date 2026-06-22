/**
 * Simulador de Nodos Transmisores para Pollitos App
 * Ejecuta este script localmente para simular el envío de datos de 4 placas ESP8266
 * al servidor Express de la aplicación.
 * 
 * Uso: node simulate_nodes.js [servidor_url]
 * Ejemplo: node simulate_nodes.js http://localhost:3000
 */

const SERVER_URL = process.argv[2] || 'http://localhost:3000';

console.log(`🤖 Iniciando simulador de sensores de pollitos...`);
console.log(`📡 Enviando datos a: ${SERVER_URL}/api/telemetry`);
console.log(`⏱️ Frecuencia: Cada 5 segundos\n`);

// Estado inicial de los 4 nodos simulados
const nodes = {
    1: { temp: 33.5, hum: 62.0, nh3: 8.5 },
    2: { temp: 34.0, hum: 58.5, nh3: 11.2 },
    3: { temp: 32.8, hum: 65.2, nh3: 14.0 },
    4: { temp: 33.1, hum: 60.1, nh3: 7.9 }
};

// Generar una fluctuación aleatoria para simular cambios de temperatura, humedad y amoníaco
function updateNodeValues(id) {
    const node = nodes[id];
    
    // Caminata aleatoria (Random Walk)
    node.temp += (Math.random() - 0.5) * 0.4;
    node.hum += (Math.random() - 0.5) * 0.8;
    node.nh3 += (Math.random() - 0.45) * 0.6; // Tendencia ligera a subir

    // Límites de simulación lógicos
    node.temp = Math.max(15.0, Math.min(45.0, node.temp));
    node.hum = Math.max(20.0, Math.min(95.0, node.hum));
    node.nh3 = Math.max(1.0, Math.min(40.0, node.nh3)); // Permitir que supere 20 ppm para testear alertas

    return {
        boardId: id,
        temp: parseFloat(node.temp.toFixed(2)),
        hum: parseFloat(node.hum.toFixed(2)),
        nh3: parseFloat(node.nh3.toFixed(2))
    };
}

// Enviar datos vía HTTP POST
async function sendTelemetry(payload) {
    try {
        const response = await fetch(`${SERVER_URL}/api/telemetry`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'X-API-Key': 'PollitosSecureIoTKey2026'
            },
            body: JSON.stringify(payload)
        });

        if (response.ok) {
            console.log(`[Nodo ${payload.boardId}] Enviado: Temp: ${payload.temp}°C | Hum: ${payload.hum}% | NH3: ${payload.nh3} ppm (Éxito)`);
        } else {
            console.error(`[Nodo ${payload.boardId}] Error al enviar. Status: ${response.status}`);
        }
    } catch (err) {
        console.error(`[Nodo ${payload.boardId}] Error de conexión:`, err.message);
    }
}

// Bucle principal de simulación
function startSimulation() {
    let activeNode = 1;

    // Enviar datos de un nodo a la vez de forma secuencial cada 1.25 segundos
    // De esta manera, cada nodo individual transmite exactamente cada 5 segundos
    setInterval(() => {
        const payload = updateNodeValues(activeNode);
        sendTelemetry(payload);
        
        activeNode = activeNode === 4 ? 1 : activeNode + 1;
    }, 1250);
}

startSimulation();
