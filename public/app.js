// Estado de la aplicación
let selectedWeek = 1;
let currentChartMetric = 'temp'; // 'temp', 'hum', 'nh3'
let historyChartInstance = null;
let lastKnownNodeStates = { 1: 'offline', 2: 'offline', 3: 'offline', 4: 'offline' };
let alertLogs = [];
let pollingIntervalId = null;
let chartIntervalId = null;
let currentSessionUsername = '';

// Umbrales de Confort Térmico por Semana
const comfortThresholds = {
    1: { minTemp: 32.0, maxTemp: 35.0, minHum: 50.0, maxHum: 70.0, maxNh3: 20.0 },
    2: { minTemp: 29.0, maxTemp: 32.0, minHum: 50.0, maxHum: 70.0, maxNh3: 20.0 },
    3: { minTemp: 26.0, maxTemp: 29.0, minHum: 50.0, maxHum: 70.0, maxNh3: 20.0 }
};

// Configuración de inicialización
document.addEventListener('DOMContentLoaded', () => {
    // Manejar formulario de Login
    const loginForm = document.getElementById('login-form');
    loginForm.addEventListener('submit', handleLogin);

    // Manejar formulario de creación de usuario
    const createUserForm = document.getElementById('create-user-form');
    createUserForm.addEventListener('submit', handleCreateUser);

    // Escuchar cambios de semana
    const weekSelect = document.getElementById('week-select');
    weekSelect.addEventListener('change', (e) => {
        selectedWeek = parseInt(e.target.value);
        updateDashboardUI(); // Actualizar evaluaciones con los nuevos umbrales
    });

    // Cargar bitácora local desde localStorage si existe
    if (localStorage.getItem('pollitos_alert_logs')) {
        alertLogs = JSON.parse(localStorage.getItem('pollitos_alert_logs'));
        renderAlertLogs();
    }

    // Verificar si ya existe una sesión iniciada
    checkSession();
});

// --- AUTENTICACIÓN Y SESIÓN ---

function checkSession() {
    const token = localStorage.getItem('pollitos_session_token');
    const username = localStorage.getItem('pollitos_session_username');
    
    const loginOverlay = document.getElementById('login-overlay');
    const appContent = document.getElementById('app-content-container');

    if (token && username) {
        currentSessionUsername = username;
        
        // Ocultar login y mostrar dashboard
        loginOverlay.classList.add('hidden');
        appContent.classList.remove('hidden');

        // Inicializar gráfico si no está inicializado
        if (!historyChartInstance) {
            initChart();
        }

        // Iniciar bucles de polling
        startPolling();
    } else {
        // Mostrar login y ocultar dashboard
        loginOverlay.classList.remove('hidden');
        appContent.classList.add('hidden');
        stopPolling();
    }
}

async function handleLogin(e) {
    e.preventDefault();
    const usernameInput = document.getElementById('login-username').value;
    const passwordInput = document.getElementById('login-password').value;
    const errorMsg = document.getElementById('login-error-msg');

    errorMsg.style.display = 'none';

    try {
        const response = await fetch('/api/auth/login', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username: usernameInput, password: passwordInput })
        });

        const data = await response.json();

        if (response.ok) {
            // Guardar sesión
            localStorage.setItem('pollitos_session_token', data.token);
            localStorage.setItem('pollitos_session_username', data.username);
            
            // Limpiar inputs
            document.getElementById('login-username').value = '';
            document.getElementById('login-password').value = '';

            checkSession();
        } else {
            errorMsg.textContent = data.error || 'Credenciales inválidas.';
            errorMsg.style.display = 'block';
        }
    } catch (err) {
        errorMsg.textContent = 'Error de conexión con el servidor.';
        errorMsg.style.display = 'block';
    }
}

function logout() {
    localStorage.removeItem('pollitos_session_token');
    localStorage.removeItem('pollitos_session_username');
    currentSessionUsername = '';
    
    // Destruir gráfico para reconstruirlo limpio al iniciar sesión de nuevo
    if (historyChartInstance) {
        historyChartInstance.destroy();
        historyChartInstance = null;
    }
    
    checkSession();
}

// Función auxiliar para obtener las cabeceras con el token JWT
function getAuthHeaders() {
    const token = localStorage.getItem('pollitos_session_token');
    return {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${token}`
    };
}

// --- POLLING DE DATOS ---

function startPolling() {
    // Detener intervalos anteriores si los hubiera
    stopPolling();

    // Actualizar inmediatamente
    updateDashboardUI();
    fetchHistoryData();

    // Polling del estado actual cada 2 segundos
    pollingIntervalId = setInterval(updateDashboardUI, 2000);
    // Polling del historial cada 15 segundos
    chartIntervalId = setInterval(fetchHistoryData, 15000);
}

function stopPolling() {
    if (pollingIntervalId) clearInterval(pollingIntervalId);
    if (chartIntervalId) clearInterval(chartIntervalId);
}

// --- ACTUALIZACIÓN DE INTERFAZ ---

async function updateDashboardUI() {
    try {
        const response = await fetch('/api/telemetry/current', {
            headers: getAuthHeaders()
        });
        
        if (response.status === 401 || response.status === 403) {
            // Token inválido o expirado, desloguear
            logout();
            return;
        }

        if (!response.ok) throw new Error('Error al consultar estado actual');
        const currentData = await response.json();
        
        const nodesData = {};
        currentData.forEach(row => {
            nodesData[row.board_id] = row;
        });

        let totalAlerts = 0;

        for (let id = 1; id <= 4; id++) {
            const card = document.getElementById(`node-card-${id}`);
            const statusIndicator = document.getElementById(`node-status-${id}`);
            const tempVal = document.getElementById(`node-temp-${id}`);
            const humVal = document.getElementById(`node-hum-${id}`);
            const nh3Val = document.getElementById(`node-nh3-${id}`);
            const footerState = document.getElementById(`node-state-${id}`);

            const nodeData = nodesData[id];
            
            // Si tiene datos y son de hace menos de 15 segundos
            const isOnline = nodeData && (new Date() - new Date(nodeData.timestamp)) < 15000;

            card.classList.remove('card-state-optimal', 'card-state-warning-temp', 'card-state-warning-hum', 'card-state-critical');

            if (isOnline) {
                tempVal.textContent = nodeData.temperature.toFixed(1);
                humVal.textContent = nodeData.humidity.toFixed(1);
                nh3Val.textContent = nodeData.nh3.toFixed(1);

                statusIndicator.textContent = "Online";
                statusIndicator.className = "status-indicator online";

                const thresholds = comfortThresholds[selectedWeek];
                let cardClass = 'card-state-optimal';
                let stateText = 'Óptimo';
                let alertType = null;
                let alertDesc = '';

                if (nodeData.nh3 >= thresholds.maxNh3) {
                    cardClass = 'card-state-critical';
                    stateText = 'Crítico: NH₃ Alto';
                    totalAlerts++;
                    alertType = 'critical';
                    alertDesc = `Amoníaco crítico: ${nodeData.nh3.toFixed(1)} ppm (Límite: ${thresholds.maxNh3} ppm)`;
                } 
                else if (nodeData.temperature < thresholds.minTemp || nodeData.temperature > thresholds.maxTemp) {
                    cardClass = 'card-state-warning-temp';
                    stateText = nodeData.temperature < thresholds.minTemp ? 'Alerta: Temp Baja' : 'Alerta: Temp Alta';
                    totalAlerts++;
                    alertType = 'warning';
                    alertDesc = `Temperatura fuera de rango: ${nodeData.temperature.toFixed(1)}°C (Óptimo: ${thresholds.minTemp}-${thresholds.maxTemp}°C)`;
                }
                else if (nodeData.humidity < thresholds.minHum || nodeData.humidity > thresholds.maxHum) {
                    cardClass = 'card-state-warning-hum';
                    stateText = 'Alerta: Humedad';
                    totalAlerts++;
                    alertType = 'warning';
                    alertDesc = `Humedad fuera de rango: ${nodeData.humidity.toFixed(0)}% (Óptimo: ${thresholds.minHum}-${thresholds.maxHum}%)`;
                }

                card.classList.add(cardClass);
                footerState.textContent = stateText;

                if (alertType && lastKnownNodeStates[id] !== stateText) {
                    addAlertLog(id, alertType, alertDesc);
                }

                lastKnownNodeStates[id] = stateText;
            } else {
                tempVal.textContent = '--';
                humVal.textContent = '--';
                nh3Val.textContent = '--';
                statusIndicator.textContent = "Sin datos";
                statusIndicator.className = "status-indicator offline";
                footerState.textContent = 'Desconectado';
                lastKnownNodeStates[id] = 'offline';
            }
        }

        const alertsBadge = document.getElementById('global-alerts-badge');
        const alertsCount = document.getElementById('active-alerts-count');
        alertsCount.textContent = totalAlerts;
        
        if (totalAlerts > 0) {
            alertsBadge.classList.add('alert-active');
        } else {
            alertsBadge.classList.remove('alert-active');
        }

    } catch (err) {
        console.error('Error al actualizar interfaz:', err.message);
    }
}

// --- BITÁCORA DE ALERTAS ---

function addAlertLog(nodeId, type, description) {
    const timestampStr = new Date().toLocaleTimeString('es-PE') + ' ' + new Date().toLocaleDateString('es-PE');
    const newAlert = { nodeId, type, description, time: timestampStr };
    
    alertLogs.unshift(newAlert);
    if (alertLogs.length > 30) alertLogs.pop();
    
    localStorage.setItem('pollitos_alert_logs', JSON.stringify(alertLogs));
    renderAlertLogs();
}

function renderAlertLogs() {
    const list = document.getElementById('alert-log-list');
    list.innerHTML = '';

    if (alertLogs.length === 0) {
        list.innerHTML = '<li class="empty-log-msg">No se han registrado anomalías.</li>';
        return;
    }

    alertLogs.forEach(alert => {
        const item = document.createElement('li');
        item.className = `log-item ${alert.type}`;
        const prefix = alert.type === 'critical' ? '🚨 CRÍTICO' : '⚠️ ALERTA';
        item.innerHTML = `
            <strong>${prefix} - Nodo ${alert.nodeId}</strong>: ${alert.description}
            <span class="time">${alert.time}</span>
        `;
        list.appendChild(item);
    });
}

function clearLocalAlertLog() {
    alertLogs = [];
    localStorage.removeItem('pollitos_alert_logs');
    renderAlertLogs();
}

// --- GESTIÓN DE USUARIOS (ADMIN MODAL) ---

function openUsersModal() {
    document.getElementById('users-modal').classList.remove('hidden');
    loadUsersList();
}

function closeUsersModal() {
    document.getElementById('users-modal').classList.add('hidden');
    // Limpiar alertas de creación
    document.getElementById('create-error-msg').style.display = 'none';
    document.getElementById('create-success-msg').style.display = 'none';
}

async function loadUsersList() {
    const tbody = document.getElementById('users-table-body');
    tbody.innerHTML = '<tr><td colspan="3" style="text-align:center;">Cargando usuarios...</td></tr>';

    try {
        const response = await fetch('/api/auth/users', { headers: getAuthHeaders() });
        if (!response.ok) throw new Error('Error al cargar la lista de usuarios');
        
        const users = await response.json();
        tbody.innerHTML = '';

        if (users.length === 0) {
            tbody.innerHTML = '<tr><td colspan="3" style="text-align:center;">No hay usuarios.</td></tr>';
            return;
        }

        users.forEach(user => {
            const tr = document.createElement('tr');
            const formattedDate = new Date(user.created_at).toLocaleDateString('es-PE');
            const isSelf = user.username.toLowerCase() === currentSessionUsername.toLowerCase();
            
            tr.innerHTML = `
                <td><strong>${user.username}</strong> ${isSelf ? '<span style="color:#ffd23f; font-size:0.75rem;">(Tú)</span>' : ''}</td>
                <td>${formattedDate}</td>
                <td>
                    <button class="delete-user-btn" 
                            onclick="deleteUser('${user.username}')" 
                            ${isSelf ? 'disabled' : ''}>
                        <i class="fa-solid fa-trash"></i> Eliminar
                    </button>
                </td>
            `;
            tbody.appendChild(tr);
        });

    } catch (err) {
        tbody.innerHTML = '<tr><td colspan="3" style="text-align:center; color:#f87171;">Error al cargar.</td></tr>';
    }
}

async function handleCreateUser(e) {
    e.preventDefault();
    const newUsernameInput = document.getElementById('new-username').value;
    const newPasswordInput = document.getElementById('new-password').value;
    const errorMsg = document.getElementById('create-error-msg');
    const successMsg = document.getElementById('create-success-msg');

    errorMsg.style.display = 'none';
    successMsg.style.display = 'none';

    try {
        const response = await fetch('/api/auth/users', {
            method: 'POST',
            headers: getAuthHeaders(),
            body: JSON.stringify({ username: newUsernameInput, password: newPasswordInput })
        });

        const data = await response.json();

        if (response.ok) {
            successMsg.style.display = 'block';
            document.getElementById('new-username').value = '';
            document.getElementById('new-password').value = '';
            loadUsersList();
        } else {
            errorMsg.textContent = data.error || 'Error al registrar el usuario.';
            errorMsg.style.display = 'block';
        }
    } catch (err) {
        errorMsg.textContent = 'Error de comunicación.';
        errorMsg.style.display = 'block';
    }
}

async function deleteUser(username) {
    if (!confirm(`¿Estás seguro de que deseas eliminar el usuario "${username}"?`)) return;

    try {
        const response = await fetch(`/api/auth/users/${username}`, {
            method: 'DELETE',
            headers: getAuthHeaders()
        });

        if (response.ok) {
            loadUsersList();
        } else {
            const data = await response.json();
            alert(data.error || 'No se pudo eliminar el usuario.');
        }
    } catch (err) {
        alert('Error de conexión al eliminar usuario.');
    }
}

// --- GRÁFICO HISTÓRICO ---

function initChart() {
    const ctx = document.getElementById('historyChart').getContext('2d');
    
    const nodeColors = {
        1: { border: '#ff7675', bg: 'rgba(255, 118, 117, 0.1)' },
        2: { border: '#74b9ff', bg: 'rgba(116, 185, 255, 0.1)' },
        3: { border: '#55efc4', bg: 'rgba(85, 239, 196, 0.1)' },
        4: { border: '#a29bfe', bg: 'rgba(162, 155, 254, 0.1)' }
    };

    const datasets = [1, 2, 3, 4].map(id => ({
        label: `Nodo ${id}`,
        data: [],
        borderColor: nodeColors[id].border,
        backgroundColor: nodeColors[id].bg,
        borderWidth: 2,
        tension: 0.3,
        fill: false,
        pointRadius: 1
    }));

    historyChartInstance = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: datasets
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    position: 'top',
                    labels: { color: '#8e9bb2', font: { family: 'Plus Jakarta Sans', size: 11 } }
                },
                tooltip: { mode: 'index', intersect: false }
            },
            scales: {
                x: {
                    grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    ticks: { color: '#8e9bb2', font: { size: 10 } }
                },
                y: {
                    grid: { color: 'rgba(255, 255, 255, 0.05)' },
                    ticks: { color: '#8e9bb2' }
                }
            }
        }
    });
}

function switchChartType(metric) {
    currentChartMetric = metric;
    
    document.getElementById('btn-chart-temp').classList.remove('active');
    document.getElementById('btn-chart-hum').classList.remove('active');
    document.getElementById('btn-chart-nh3').classList.remove('active');
    
    document.getElementById(`btn-chart-${metric}`).classList.add('active');
    
    fetchHistoryData();
}

async function fetchHistoryData() {
    if (!historyChartInstance) return;
    
    try {
        const response = await fetch('/api/telemetry/history?limit=200', {
            headers: getAuthHeaders()
        });

        if (response.status === 401 || response.status === 403) {
            logout();
            return;
        }

        if (!response.ok) throw new Error('Error al consultar el historial');
        const historyData = await response.json();
        
        const timeGroups = {};
        
        historyData.forEach(row => {
            const timeLabel = new Date(row.timestamp).toLocaleTimeString('es-PE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
            
            if (!timeGroups[timeLabel]) {
                timeGroups[timeLabel] = { label: timeLabel, 1: null, 2: null, 3: null, 4: null };
            }
            
            let val = 0;
            if (currentChartMetric === 'temp') val = row.temperature;
            else if (currentChartMetric === 'hum') val = row.humidity;
            else if (currentChartMetric === 'nh3') val = row.nh3;
            
            timeGroups[timeLabel][row.board_id] = val;
        });

        const sortedLabels = Object.keys(timeGroups);
        const nodeDataLists = { 1: [], 2: [], 3: [], 4: [] };
        
        sortedLabels.forEach(timeLabel => {
            const dataObj = timeGroups[timeLabel];
            for (let id = 1; id <= 4; id++) {
                nodeDataLists[id].push(dataObj[id]);
            }
        });

        historyChartInstance.data.labels = sortedLabels;
        for (let id = 1; id <= 4; id++) {
            historyChartInstance.data.datasets[id - 1].data = nodeDataLists[id];
            
            let metricName = 'Temp (°C)';
            if (currentChartMetric === 'hum') metricName = 'Humedad (%)';
            if (currentChartMetric === 'nh3') metricName = 'Amoníaco (ppm)';
            historyChartInstance.data.datasets[id - 1].label = `Nodo ${id} - ${metricName}`;
        }
        
        historyChartInstance.update('none');

    } catch (err) {
        console.error('Error al cargar datos históricos para gráfico:', err.message);
    }
}
