const sqlite3 = require('sqlite3').verbose();
const path = require('path');
const crypto = require('crypto');

const dbPath = path.join(__dirname, 'database.sqlite');
const db = new sqlite3.Database(dbPath, (err) => {
    if (err) {
        console.error('Error al abrir la base de datos SQLite:', err.message);
    } else {
        console.log('Conectado a la base de datos SQLite local.');
        createTables();
    }
});

// Función para generar una sal aleatoria
function generateSalt() {
    return crypto.randomBytes(16).toString('hex');
}

// Función para hashear la contraseña usando PBKDF2
function hashPassword(password, salt) {
    return crypto.pbkdf2Sync(password, salt, 1000, 64, 'sha256').toString('hex');
}

function createTables() {
    db.serialize(() => {
        // 1. Tabla de Telemetría
        db.run(`
            CREATE TABLE IF NOT EXISTS telemetry (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                board_id INTEGER NOT NULL,
                temperature REAL NOT NULL,
                humidity REAL NOT NULL,
                nh3 REAL NOT NULL,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
            )
        `, (err) => {
            if (err) console.error('Error al crear tabla telemetry:', err.message);
        });

        // 2. Tabla de Usuarios
        db.run(`
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT UNIQUE NOT NULL,
                password_hash TEXT NOT NULL,
                salt TEXT NOT NULL,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            )
        `, (err) => {
            if (err) {
                console.error('Error al crear tabla users:', err.message);
            } else {
                // Sembrar usuario por defecto si la tabla está vacía
                seedDefaultUser();
            }
        });
    });
}

// Sembrar el usuario por defecto: pollitos2026 / 982778218
function seedDefaultUser() {
    db.get('SELECT COUNT(*) as count FROM users', [], (err, row) => {
        if (err) {
            console.error('Error al verificar usuarios existentes:', err.message);
            return;
        }

        if (row.count === 0) {
            const defaultUser = 'pollitos2026';
            const defaultPass = '982778218';
            const salt = generateSalt();
            const hash = hashPassword(defaultPass, salt);

            db.run(`
                INSERT INTO users (username, password_hash, salt)
                VALUES (?, ?, ?)
            `, [defaultUser, hash, salt], (err) => {
                if (err) {
                    console.error('Error al sembrar usuario por defecto:', err.message);
                } else {
                    console.log(`👤 Usuario administrador creado por defecto -> Usuario: ${defaultUser} | Clave: ${defaultPass}`);
                }
            });
        }
    });
}

// --- MÉTODOS DE TELEMETRÍA ---

function saveReading(boardId, temp, hum, nh3) {
    return new Promise((resolve, reject) => {
        const query = `
            INSERT INTO telemetry (board_id, temperature, humidity, nh3, timestamp)
            VALUES (?, ?, ?, ?, datetime('now', 'localtime'))
        `;
        db.run(query, [boardId, temp, hum, nh3], function(err) {
            if (err) {
                reject(err);
            } else {
                resolve({ id: this.lastID });
            }
        });
    });
}

function getLatestReadings() {
    return new Promise((resolve, reject) => {
        const query = `
            SELECT t1.* 
            FROM telemetry t1
            INNER JOIN (
                SELECT board_id, MAX(timestamp) as max_ts
                FROM telemetry
                GROUP BY board_id
            ) t2 ON t1.board_id = t2.board_id AND t1.timestamp = t2.max_ts
            ORDER BY t1.board_id ASC
        `;
        db.all(query, [], (err, rows) => {
            if (err) {
                reject(err);
            } else {
                resolve(rows);
            }
        });
    });
}

function getHistory(limit = 100) {
    return new Promise((resolve, reject) => {
        const query = `
            SELECT * FROM telemetry
            ORDER BY timestamp DESC
            LIMIT ?
        `;
        db.all(query, [limit], (err, rows) => {
            if (err) {
                reject(err);
            } else {
                resolve(rows.reverse());
            }
        });
    });
}

// --- MÉTODOS DE USUARIOS ---

function getUser(username) {
    return new Promise((resolve, reject) => {
        db.get('SELECT * FROM users WHERE username = ?', [username], (err, row) => {
            if (err) reject(err);
            else resolve(row);
        });
    });
}

function createUser(username, password) {
    return new Promise((resolve, reject) => {
        const salt = generateSalt();
        const hash = hashPassword(password, salt);

        db.run(`
            INSERT INTO users (username, password_hash, salt)
            VALUES (?, ?, ?)
        `, [username.toLowerCase(), hash, salt], function(err) {
            if (err) {
                reject(err);
            } else {
                resolve({ id: this.lastID });
            }
        });
    });
}

function deleteUser(username) {
    return new Promise((resolve, reject) => {
        db.run('DELETE FROM users WHERE username = ?', [username.toLowerCase()], function(err) {
            if (err) reject(err);
            else resolve({ changes: this.changes });
        });
    });
}

function getAllUsers() {
    return new Promise((resolve, reject) => {
        db.all('SELECT id, username, created_at FROM users ORDER BY username ASC', [], (err, rows) => {
            if (err) reject(err);
            else resolve(rows);
        });
    });
}

module.exports = {
    saveReading,
    getLatestReadings,
    getHistory,
    getUser,
    createUser,
    deleteUser,
    getAllUsers,
    hashPassword
};
