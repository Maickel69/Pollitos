const express = require('express');
const app = express();
const PORT = 3000; // Puerto obligatorio del contenedor

// Respuesta básica en la raíz
app.get('/', (req, res) => {
    res.send('<h1>¡Servidor de Pollitos funcionando perfectamente pio pio! 🐥</h1>');
});

app.listen(PORT, () => {
    console.log(`Servidor corriendo en http://localhost:${PORT}`);
});
