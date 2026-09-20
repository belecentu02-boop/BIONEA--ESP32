#ifndef MODO_CAMPO_H
#define MODO_CAMPO_H

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

// SERVIDOR WEB DEL .INO
extern WebServer server;

// VARIABLES GLOBALES DEL .INO
extern bool sesionIniciada;
extern int minutosSesion;
extern int intervaloSegundos;
extern String individuoCodigo;
extern String especieActual;
extern String sessionId;
extern String nombreArchivoActual;
extern String deviceMAC;
extern bool inicializarCSV(String nombreArchivo);
extern bool modoOnline;

// FUNCIONES DEL .INO QUE NECESITAMOS:
extern void limpiarSesion();

// HTML de la página web local para el celular en el campo
const char HTML_MODO_CAMPO[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>BIONEA - Modo Campo</title>
  <style>
    body { font-family: Arial, sans-serif; background: #121212; color: #fff; padding: 20px; }
    .card { background: #1e1e1e; padding: 20px; border-radius: 10px; max-width: 400px; margin: auto; }
    h2 { color: #4CAF50; text-align: center; margin-bottom: 15px; }
    label { display: block; margin-top: 10px; font-size: 14px; color: #ccc; }
    input { width: 100%; padding: 10px; margin-top: 5px; border-radius: 5px; border: 1px solid #444; background: #2a2a2a; color: #fff; box-sizing: border-box; }
    button { width: 100%; padding: 12px; background: #4CAF50; border: none; color: white; font-weight: bold; margin-top: 20px; border-radius: 5px; cursor: pointer; font-size: 16px; }
  </style>
</head>
<body>
  <div class="card">
    <h2>🦎 Configuración CAMPO</h2>
    <form action="/iniciar_campo" method="POST">
      <label>Código Individuo:</label>
      <input type="text" name="individuo" placeholder="Ej: IND-001" required>
      
      <label>Especie:</label>
      <input type="text" name="especie" placeholder="Ej: Liolaemus" required>
      
      <label>Duración (Minutos):</label>
      <input type="number" name="duracion" value="30" min="1" required>
      
      <label>Intervalo Muestreo (Segundos):</label>
      <input type="number" name="intervalo" value="35" min="35" required>
      
      <button type="submit">INICIAR SESIÓN LOCAL</button>
    </form>
  </div>
</body>
</html>
)rawliteral";

inline void handleCampoRoot() {
  server.send(200, "text/html", HTML_MODO_CAMPO);
}

inline void handleIniciarCampo() {
  if (server.hasArg("individuo") && server.hasArg("especie")) {
    individuoCodigo = server.arg("individuo");
    especieActual = server.arg("especie");
    minutosSesion = server.arg("duracion").toInt();
    intervaloSegundos = server.arg("intervalo").toInt();

    sessionId = "CAMPO_" + individuoCodigo;
    nombreArchivoActual = "CAMPO_" + individuoCodigo + ".csv";

    if (inicializarCSV(nombreArchivoActual)) {
      modoOnline = false; // Forzar modo offline estricto
      sesionIniciada = true;
      
      server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='3;url=/'></head><body style='background:#121212;color:#fff;font-family:sans-serif;text-align:center;padding-top:50px;'><h2 style='color:#4CAF50;'>✅ ¡Sesión iniciada en MicroSD!</h2><p>La página se recargará automáticamente para el siguiente individuo...</p></body></html>");
      Serial.println("[CAMPO] Sesión iniciada localmente desde el Servidor Web AP.");
    } else {
      server.send(500, "text/plain", "Error al crear archivo en la tarjeta SD.");
    }
  } else {
    server.send(400, "text/plain", "Faltan parámetros requeridos.");
  }
}

inline void iniciarAPModoCampo() {
  WiFi.mode(WIFI_AP);
  String ssid = "BIONEA-CAMPO-" + deviceMAC;
  ssid.replace(":", "");
  WiFi.softAP(ssid.c_str());

  Serial.println("[CAMPO] Punto de Acceso (AP) creado con éxito");
  Serial.print("[CAMPO] Conéctate a la red Wi-Fi e ingresa a IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleCampoRoot);
  server.on("/iniciar_campo", HTTP_POST, handleIniciarCampo);
  server.begin();
}

#endif