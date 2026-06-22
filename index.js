const express = require('express');
const cors = require('cors');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const db = require('./database');

const app = express();
const PORT = 3000;

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Cargar configuración local
let config = { googleSheetsUrl: '', nh3Threshold: 20.0, apiKey: 'PollitosSecureIoTKey2026', jwtSecret: 'PollitosSecretJWTKey2026' };
const configPath = path.join(__dirname, 'config.json');

function loadConfig() {
    try {
        if (fs.existsSync(configPath)) {
            const fileData = fs.readFileSync(configPath, 'utf8');
            config = JSON.parse(fileData);
            console.log('Configuración cargada con éxito:', config);
        }
    } catch (err) {
        console.error('Error al cargar config.json:', err.message);
    }
}
loadConfig();

// Recargar configuración si cambia
fs.watch(configPath, (eventType) => {
    if (eventType === 'change') {
        console.log('config.json ha cambiado, recargando...');
        loadConfig();
    }
});

// --- MIDDLEWARES DE SEGURIDAD ---

// Middleware para verificar la API Key de los dispositivos IoT
function validateApiKey(req, res, next) {
    const receivedKey = req.headers['x-api-key'] || req.query.apiKey;
    if (!config.apiKey || receivedKey === config.apiKey) {
        next();
    } else {
        console.warn('❌ Petición de escritura rechazada: API Key inválida o ausente.');
        res.status(403).json({ error: 'Prohibido: API Key inválida o ausente' });
    }
}

// Middleware para verificar el Token de Sesión del Dashboard
function authenticateToken(req, res, next) {
    const authHeader = req.headers['authorization'];
    const token = authHeader && authHeader.split(' ')[1]; // Bearer <token>

    if (!token) {
        return res.status(401).json({ error: 'No autorizado: Falta el token de sesión' });
    }

    const parts = token.split('.');
    if (parts.length !== 2) {
        return res.status(401).json({ error: 'No autorizado: Token de formato inválido' });
    }

    const [username, signature] = parts;
    
    // Validar firma usando HMAC-SHA256
    const expectedSignature = crypto
        .createHmac('sha256', config.jwtSecret)
        .update(username)
        .digest('hex');

    if (signature === expectedSignature) {
        req.username = username;
        next();
    } else {
        res.status(403).json({ error: 'Prohibido: Token de sesión inválido o expirado' });
    }
}

// Función auxiliar para firmar un token de sesión
function generateSessionToken(username) {
    const signature = crypto
        .createHmac('sha256', config.jwtSecret)
        .update(username)
        .digest('hex');
    return `${username}.${signature}`;
}

// --- RUTAS DE AUTENTICACIÓN Y USUARIOS ---

// 1. Iniciar sesión
app.post('/api/auth/login', async (req, res) => {
    const { username, password } = req.body;

    if (!username || !password) {
        return res.status(400).json({ error: 'Falta usuario o contraseña' });
    }

    try {
        const user = await db.getUser(username.toLowerCase());
        if (!user) {
            return res.status(401).json({ error: 'Usuario o contraseña incorrectos' });
        }

        // Hashear la contraseña entrante usando la sal guardada
        const hash = db.hashPassword(password, user.salt);
        
        if (hash === user.password_hash) {
            // Generar token ligero firmado
            const token = generateSessionToken(user.username);
            console.log(`👤 Sesión iniciada para el usuario: ${user.username}`);
            res.status(200).json({ token, username: user.username });
        } else {
            res.status(401).json({ error: 'Usuario o contraseña incorrectos' });
        }
    } catch (err) {
        console.error('Error en login:', err.message);
        res.status(500).json({ error: 'Error interno en el servidor de autenticación' });
    }
});

// 2. Obtener lista de usuarios (Protegido)
app.get('/api/auth/users', authenticateToken, async (req, res) => {
    try {
        const users = await db.getAllUsers();
        res.status(200).json(users);
    } catch (err) {
        res.status(500).json({ error: 'Error al consultar lista de usuarios' });
    }
});

// 3. Crear nuevo usuario (Protegido)
app.post('/api/auth/users', authenticateToken, async (req, res) => {
    const { username, password } = req.body;

    if (!username || !password || username.trim().length < 3 || password.trim().length < 4) {
        return res.status(400).json({ error: 'El usuario debe tener al menos 3 caracteres y la contraseña 4.' });
    }

    try {
        const existing = await db.getUser(username.toLowerCase());
        if (existing) {
            return res.status(400).json({ error: 'El nombre de usuario ya está registrado.' });
        }

        await db.createUser(username, password);
        console.log(`👤 Usuario creado: ${username} por administrador ${req.username}`);
        res.status(201).json({ status: 'success', message: 'Usuario creado con éxito' });
    } catch (err) {
        res.status(500).json({ error: 'Error al registrar el nuevo usuario.' });
    }
});

// 4. Eliminar un usuario (Protegido)
app.delete('/api/auth/users/:username', authenticateToken, async (req, res) => {
    const userToDelete = req.params.username.toLowerCase();

    // Evitar que el administrador se autoelimine
    if (userToDelete === req.username.toLowerCase()) {
        return res.status(400).json({ error: 'No puedes eliminar el usuario con el que tienes sesión iniciada.' });
    }

    try {
        const result = await db.deleteUser(userToDelete);
        if (result.changes === 0) {
            return res.status(404).json({ error: 'Usuario no encontrado' });
        }
        console.log(`👤 Usuario eliminado: ${userToDelete} por administrador ${req.username}`);
        res.status(200).json({ status: 'success', message: 'Usuario eliminado correctamente' });
    } catch (err) {
        res.status(500).json({ error: 'Error al eliminar el usuario' });
    }
});


// --- RUTAS DE TELEMETRÍA ---

// 1. Recibir datos del receptor principal (ESP32) - Protegido por API KEY
app.post('/api/telemetry', validateApiKey, async (req, res) => {
    const { boardId, temp, hum, nh3 } = req.body;

    if (boardId === undefined || temp === undefined || hum === undefined || nh3 === undefined) {
        return res.status(400).json({ error: 'Faltan parámetros requeridos (boardId, temp, hum, nh3)' });
    }

    const numericBoardId = parseInt(boardId);
    const numericTemp = parseFloat(temp);
    const numericHum = parseFloat(hum);
    const numericNh3 = parseFloat(nh3);

    try {
        // Guardar localmente
        await db.saveReading(numericBoardId, numericTemp, numericHum, numericNh3);

        const readingData = {
            boardId: numericBoardId,
            temp: numericTemp,
            hum: numericHum,
            nh3: numericNh3,
            timestamp: new Date().toLocaleString('es-PE', { timeZone: 'America/Lima' })
        };

        // Enviar alerta crítica si NH3 sube
        if (numericNh3 >= config.nh3Threshold) {
            console.log(`⚠️ ALERTA CRÍTICA: Amoníaco elevado en Nodo ${numericBoardId} (${numericNh3} ppm)`);
        }

        // Reenviar asíncronamente a Google Sheets
        if (config.googleSheetsUrl) {
            fetch(config.googleSheetsUrl, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(readingData)
            }).catch(err => console.error('Error al sincronizar con Google Sheets:', err.message));
        }

        res.status(200).json({ status: 'success', message: 'Lectura procesada con éxito' });
    } catch (err) {
        console.error('Error al guardar telemetría:', err.message);
        res.status(500).json({ error: 'Error al guardar datos' });
    }
});

// 2. Obtener estado actual (Protegido por Token de sesión)
app.get('/api/telemetry/current', authenticateToken, async (req, res) => {
    try {
        const readings = await db.getLatestReadings();
        res.status(200).json(readings);
    } catch (err) {
        res.status(500).json({ error: 'Error al consultar datos recientes' });
    }
});

// 3. Obtener historial (Protegido por Token de sesión)
app.get('/api/telemetry/history', authenticateToken, async (req, res) => {
    const limit = req.query.limit ? parseInt(req.query.limit) : 100;
    try {
        const history = await db.getHistory(limit);
        res.status(200).json(history);
    } catch (err) {
        res.status(500).json({ error: 'Error al consultar historial' });
    }
});

// Iniciar servidor
app.listen(PORT, () => {
    console.log(`🚀 Servidor de Pollitos corriendo en http://localhost:${PORT}`);
});
