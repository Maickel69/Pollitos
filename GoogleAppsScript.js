/**
 * Código de Google Apps Script para Google Sheets
 * 
 * INSTRUCCIONES DE INSTALACIÓN:
 * 1. En Google Drive, crea una nueva "Hoja de cálculo de Google" (Google Sheet).
 * 2. Nombra la primera fila de la primera columna con los siguientes encabezados:
 *    A1: Fecha y Hora | B1: ID del Nodo | C1: Temperatura (°C) | D1: Humedad (%) | E1: Amoníaco (ppm)
 * 3. En el menú superior de la hoja de cálculo, ve a: Extensiones -> Apps Script.
 * 4. Borra todo el código del editor y pega este script.
 * 5. Haz clic en "Implementar" (Deploy) -> "Nueva implementación" (New deployment).
 * 6. En el engranaje de configuración, selecciona "Aplicación web" (Web App).
 * 7. Configura las opciones como sigue:
 *    - Descripción: API de Pollitos App
 *    - Ejecutar como: Tú (tu correo electrónico de Gmail)
 *    - Quién tiene acceso: Cualquiera (Anyone) -> NOTA: Esto es crucial para permitir peticiones externas.
 * 8. Presiona "Implementar" y autoriza los permisos requeridos por tu cuenta de Google.
 * 9. Copia la "URL de la aplicación web" proporcionada y pégala en el archivo `config.json` de tu VPS.
 */

function doPost(e) {
  try {
    // Parsear el JSON entrante
    var data = JSON.parse(e.postData.contents);
    
    // Obtener la hoja de cálculo activa
    var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
    
    // Preparar los valores a insertar en la fila
    // Columnas: Fecha, ID Nodo, Temperatura, Humedad, Amoníaco
    var rowData = [
      data.timestamp || new Date().toLocaleString("es-PE", {timeZone: "America/Lima"}),
      data.boardId,
      data.temp,
      data.hum,
      data.nh3
    ];
    
    // Insertar al final de la hoja
    sheet.appendRow(rowData);
    
    // Retornar éxito
    return ContentService.createTextOutput(JSON.stringify({
      "status": "success",
      "message": "Fila insertada con éxito"
    })).setMimeType(ContentService.MimeType.JSON);
    
  } catch (error) {
    // Retornar error en caso de fallo
    return ContentService.createTextOutput(JSON.stringify({
      "status": "error",
      "message": error.message
    })).setMimeType(ContentService.MimeType.JSON);
  }
}
