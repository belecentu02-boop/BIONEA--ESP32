#ifndef MODO_LABORATORIO_H
#define MODO_LABORATORIO_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// CREDENCIALES LLAMADAS DESDE CONFIG_RED.H
extern const char* SERVER_URL;
extern const char* API_KEY;

// VARIABLES / OBJETOS DEL .INO
extern String sessionId;
extern String individuoCodigo;
extern String especieActual;
extern String deviceMAC;
extern float tempMin;
extern float tempMax;
extern int minutosSesion;
extern int intervaloSegundos;
extern int medicionNum;
extern bool modoOnline;
extern bool sesionIniciada;

// hardware definidas en el .ino
extern RTC_DS3231 rtc;

// funciones del .ino que este módulo necesita ejecutar
extern void errorCritico();
extern void limpiarSesion();
extern bool guardarEnSD(String linea);
extern void guardarPendiente(String linea);
extern bool hayPendientes();

int enviarMedicion(String body) {
  
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  String url = String(SERVER_URL) + "/bionea/guardar";
  http.begin(client, url);

  http.addHeader("Content-Type", "application/json");

  http.addHeader("X-API-Key", API_KEY);

  http.setConnectTimeout(10000);
  http.setTimeout(15000);

  int codigoRespuesta = http.POST(body);

  Serial.print("[HTTP] POST /bionea/guardar -> ");
  Serial.println(codigoRespuesta);

  if (codigoRespuesta < 0) {
    Serial.print("[HTTP] Error: ");
    Serial.println(HTTPClient::errorToString(codigoRespuesta));
  }

  http.end();

  return codigoRespuesta;
}

String armarJsonMedicion(String idMedicion, String fecha, String hora, float temp) {

  String alerta =
    (temp < tempMin || temp > tempMax)
      ? "FUERA DE RANGO"
      : "OK";

  StaticJsonDocument<512> doc;

  doc["id_medicion"] = idMedicion;
  doc["session_id"] = sessionId;
  doc["tipo"] = "medicion";
  doc["fecha"] = fecha;
  doc["hora"] = hora;
  doc["individuo"] = individuoCodigo;
  doc["especie"] = especieActual;
  doc["temperatura"] = temp;
  doc["temp_min"] = tempMin;
  doc["temp_max"] = tempMax;
  doc["alerta"] = alerta;

  String body;

  serializeJson(doc, body);

  return body;
}

String crearJsonDesdeLinea(String linea) {
  //toma como parámetro el string "Linea", y la convierte en otro string pero en formato JSON

  int c1 = linea.indexOf(',');
  //indexOf busca la posicion dentro de un String,
  //en este caso, en base a las comas, pero empieza a buscar despues del c1
  int c2 = linea.indexOf(',', c1 + 1);
  int c3 = linea.indexOf(',', c2 + 1);
  int c4 = linea.indexOf(',', c3 + 1);
  int c5 = linea.indexOf(',', c4 + 1);
  int c6 = linea.indexOf(',', c5 + 1);
  int c7 = linea.indexOf(',', c6 + 1);
  int c8 = linea.indexOf(',', c7 + 1);
  int c9 = linea.indexOf(',', c8 + 1);

  if (
    //si por alguna razón algun campo o coma no existe, retorna vacío
    c1 == -1 || c2 == -1 || c3 == -1 ||
    c4 == -1 || c5 == -1 || c6 == -1 ||
    c7 == -1 || c8 == -1 || c9 == -1 
  ) {
    Serial.println("[SYNC] ❌ Formato de pendiente invalido");
    return "";
  }

  String idMedicion =
    linea.substring(0, c1);

  String sid =
    linea.substring(c1 + 1, c2);

  String individuo =
    linea.substring(c2 + 1, c3);

  String especie =
    linea.substring(c3 + 1, c4);

  String fecha =
    linea.substring(c4 + 1, c5);

  String hora =
    linea.substring(c5 + 1, c6);

  String temperatura =
    linea.substring(c6 + 1, c7);

  String tmin =
    linea.substring(c7 + 1, c8);

  String tmax =
    linea.substring(c8 + 1, c9);

  String alerta =
    linea.substring(c9 + 1);


  StaticJsonDocument<512> doc;

  doc["id_medicion"] = idMedicion;
  doc["session_id"] = sid;
  doc["tipo"] = "medicion";
  doc["fecha"] = fecha;
  doc["hora"] = hora;
  doc["individuo"] = individuo;
  doc["especie"] = especie;
  doc["temperatura"] = temperatura.toFloat();
  doc["temp_min"] = tmin.toFloat();
  doc["temp_max"] = tmax.toFloat();
  doc["alerta"] = alerta;

  String body;

  serializeJson(doc, body);

  return body;
}

// hace una peticion get HTTPS al servidor para consultar si hay una neuva sesion 
// de monitoreo asignada a la direccion MAC del esp32
void consultarSesionAsignada() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  String url =
    String(SERVER_URL) +
    "/bionea/sesion?mac=" +
    deviceMAC;

  http.begin(client, url);
  http.addHeader("X-API-Key", API_KEY);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);

  int codigo = http.GET();

  Serial.print("[POLL] Respuesta del servidor: ");
  Serial.println(codigo);
  // si el servidor respondio HTTP 200, parsea el JSON recibido y extrae las variables requeridas
  // sessionId, individuoCodigo, especieActual, duracion e intervalo de monitoreo y temp min y max
  if (codigo == 200) {

    String payload = http.getString();

    Serial.println("[POLL] Sesion recibida:");
    Serial.println(payload);

    StaticJsonDocument<512> doc;

    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("[POLL] Error al leer JSON: ");
      Serial.println(error.c_str());
      http.end();
      return;
    }

    sessionId = doc["session_id"].as<String>();
    individuoCodigo = doc["individuo"].as<String>();
    especieActual = doc["especie"].as<String>();

    int duracionRecibida = doc["duracion"] | 60;
    int intervaloRecibido = doc["intervalo"] | 1;

    float tempMinRecibida =
      doc["temp_min"].isNull() ? 0 : doc["temp_min"].as<float>();

    float tempMaxRecibida =
      doc["temp_max"].isNull() ? 0 : doc["temp_max"].as<float>();

    minutosSesion = duracionRecibida;
    intervaloSegundos = intervaloRecibido * 60;

    tempMin = tempMinRecibida;
    tempMax = tempMaxRecibida;

    Serial.println("------- DATOS DEL DASHBOARD -------");

    Serial.print("Session ID: ");
    Serial.println(sessionId);

    Serial.print("Individuo: ");
    Serial.println(individuoCodigo);

    Serial.print("Especie: ");
    Serial.println(especieActual);

    Serial.print("Duracion: ");
    Serial.print(duracionRecibida);
    Serial.println(" min");

    Serial.print("Intervalo recibido: ");
    Serial.print(intervaloRecibido);
    Serial.println(" min");

    Serial.print("Temp minima: ");
    Serial.println(tempMinRecibida);

    Serial.print("Temp maxima: ");
    Serial.println(tempMaxRecibida);

    Serial.println("-----------------------------------");
  
    // genera los nombres de archivos csv para la sesion local y pnd para sus respaldos offline, basandose en la fecha y hora actual del RTC
    // crea el archivo en la microSD y marca sesionIniciada como verdadero
    if (!sesionIniciada) {
      
      DateTime ahora = rtc.now();

      char nombreBase[16];
      char nombreBasePendiente[9];

      sprintf(
        nombreBase,
        "%02d%02d%02d%02d%02d",
        ahora.month(),
        ahora.day(),
        ahora.hour(),
        ahora.minute(),
        ahora.second()
      );

      sprintf(
        nombreBasePendiente,
        "%02d%02d%02d%02d",
        ahora.month(),
        ahora.day(),
        ahora.hour(),
        ahora.minute()
      );

      nombreArchivoActual = String(nombreBase) + ".csv";
      nombreArchivoPendientes = String(nombreBasePendiente) + ".pnd";

      medicionNum = 0;
      erroresSensorConsecutivos = 0;
      sesionInterrumpidaPorError = false;

      if (!inicializarCSV(nombreArchivoActual)) {
        Serial.println("[ERROR CRITICO] No se pudo crear el archivo de la sesion");
        errorCritico();
        http.end();
        return;
      }

      sesionIniciada = true;

      Serial.println("[PANEL] Sesion iniciada desde dashboard");
    }
  }

  else if (codigo == 204) {
    Serial.println("[POLL] No hay sesion asignada.");
  }

  else {
    Serial.println("[POLL] Respuesta inesperada.");
  }

  http.end();
}

//Lo que hace esta función es gestionar la conexion para que cuando vuelva podramos actualizar las variables y avisar que
//regresó la conexion para seguir midiendo y guardando esos datos.
void gestionarConexion() {
  //identificar el estado actual del wifi ¿Está conectado?
  bool conectadoAhora = WiFi.status() == WL_CONNECTED;
  modoOnline = conectadoAhora;
  //copia el estado del bool acá, asi despues podemos averiguar
  //si estamos o no conectados.

  if (modoOnlineAnterior && !modoOnline) {
    //si antes estaba online pero ahora no 
    Serial.println("Wifi Perdido");
  }
  if (!modoOnlineAnterior && modoOnline) {
    //si antes estaba perdido pero ahora recuperamos...
    Serial.println("Wifi recuperado");

    if (hayPendientes()) {
      //si hay pendientes, prendé el led
      
    }
    else {
      //si no hay, apagalo
      
    }
  }

  modoOnlineAnterior = modoOnline;
  //si se recuperó el wifi, volvamos al estado original.
}

bool hayPendientes() {
  //existe el archivo pnd?
  return SD.exists("/" + nombreArchivoPendientes);
}

void sincronizarPendientes() {

  if (!SD.exists("/" + nombreArchivoPendientes)) {
    return; //esto pregunta, no existe archivo .pnd? y si no, retorna
  }

  File pendientes = //abrimos el archivo pero lo LEEMOS para saber que filas no se subieron
    SD.open("/" + nombreArchivoPendientes, FILE_READ);

  if (!pendientes) { //si no podemos abrir el archivo, respondemos
    Serial.println("[SYNC] No se pudo abrir el archivo pendiente");
    return;
  }

  String nombreTemporal = "/TEMP.PND"; //temp.pnd funciona como doble de seguridad, ya que si falló la subida de 5 filas
  //y quedan 2 más a subir, se guarda esa info temporariamente en temp.

  if (SD.exists(nombreTemporal)) { //si ya existía un archivo, lo borramos
    SD.remove(nombreTemporal);
  }

  File temporal = //acá lo creamos para escribir en él
    SD.open(nombreTemporal, FILE_WRITE);

  if (!temporal) { //si no se pudo crear
    Serial.println("[SYNC] No se pudo crear TEMP.PND");
    pendientes.close();
    return;
  }

  String encabezado =
    pendientes.readStringUntil('\n'); //lee hasta que llegue al saltro de linea o sea, hasta el encabezado

  encabezado.trim(); //le saca todos los espacios
  temporal.println(encabezado); //copia el encabezado en temp.pnd

  int intentosRealizados = 0;
  bool detenerIntentos = false;
  bool quedanPendientes = false;

  while (pendientes.available()) {

    String linea =
      pendientes.readStringUntil('\n');

    linea.trim();

    if (linea.length() == 0) {
      continue;
    }

    if (intentosRealizados < 5 && !detenerIntentos) {

      String body =
        crearJsonDesdeLinea(linea);

      if (body == "") {
        temporal.println(linea);
        quedanPendientes = true;
        continue;
      }

      int codigoRespuesta =
        enviarMedicion(body);

      intentosRealizados++;

      if (codigoRespuesta >= 200 && codigoRespuesta < 300) {

      } else {
        // Sigue pendiente
        quedanPendientes = true;
        temporal.println(linea);

        // Si además se perdió realmente el WiFi,
        // dejamos de intentar el resto del lote
        if (WiFi.status() != WL_CONNECTED) {
          modoOnline = false;
          detenerIntentos = true;
        }
      }

    } else {
      //si ya se intentaron 5 veces o si perdio el wifi
      //conservamos la fila sin enviarla
      temporal.println(linea);
      quedanPendientes = true;
    }
  }

  pendientes.close();
  temporal.close();

  String rutaPendientes = "/" + nombreArchivoPendientes;

  if (!quedanPendientes) {
    SD.remove(rutaPendientes);
    SD.remove(nombreTemporal);

    Serial.println("[SYNC] ✅ Todas las pendientes fueron enviadas");
    return;
  }

  // Borrar la cola anterior
  if (!SD.remove(rutaPendientes)) {
    Serial.println("[SYNC] ❌ No se pudo borrar el PND anterior");
    SD.remove(nombreTemporal);
    return;
  }

  // Convertir TEMP.PND en el nuevo archivo de pendientes
  if (!SD.rename(nombreTemporal, rutaPendientes)) {
    Serial.println("[SYNC] ❌ No se pudo renombrar TEMP.PND");
    return;
  }

  Serial.print("[SYNC] Lote terminado. Solicitudes HTTP realizadas: ");

  Serial.println(intentosRealizados);
}

void sincronizarCierresPendientes() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  File raiz = SD.open("/");
  if (!raiz || !raiz.isDirectory()) {
    Serial.println("[SYNC FIN] No se pudo abrir la raíz de la SD");
    return;
  }
  File archivo = raiz.openNextFile();
  while (archivo) {
    String nombre = archivo.name();

    if (nombre.startsWith("/")) {
      nombre.remove(0, 1);
    }

    if (nombre.startsWith("FIN_") && nombre.endsWith(".pnd")) {
      Serial.println("[SYNC FIN] Procesando archivo pendiente: " + nombre);
      File finFile = SD.open("/" + nombre, FILE_READ);
      if (finFile) {
        String body = finFile.readStringUntil('\n');
        body.trim();
        finFile.close();

        if (body.length() > 0) {
          int codigo = enviarMedicion(body);

          if (codigo >= 200 && codigo < 300) {
            Serial.println("[SYNC FIN] Cierre sincronizado con éxito :)");
            SD.remove("/" + nombre);
          } else {
            Serial.println("[SYNC FIN] Falló el envío del cierre de sesión. (Código: " + String(codigo) + "). Se reintentará luego.");
          }
        }
      }
    }
    archivo.close();
    archivo = raiz.openNextFile();
  }
  raiz.close();
}

void sincronizarPendientesAnteriores() {
  Serial.println("[INICIO] Buscando archivos pendientes anteriores...");

  File raiz = SD.open("/");

  if (!raiz) {
    Serial.println("[INICIO] No se pudo abrir la raiz de la microSD");
    return;
  }

  if (!raiz.isDirectory()) {
    Serial.println("[INICIO] La raíz de la microSD no es un directorio");
    raiz.close();
    return;
  }

  //primero guardamos los nombres que encontramos, para no reescribir ni eliminar aquellos que no son .pnd
  String archivosEncontrados[20];
  int cantidadEncontrada = 0;

  File archivo = raiz.openNextFile();

  while (archivo) {
    if (!archivo.isDirectory()) {
      String nombre = archivo.name();

      //si el el nombre del archivo devuelve un ponele "/archivo.pnd"
      //para evitar confusiones, borramos esa "/"
      if (nombre.startsWith("/")) {
        nombre.remove(0, 1);
      }

      String nombreMinuscula = nombre;
      nombreMinuscula.toLowerCase();

      if (nombreMinuscula.endsWith(".pnd") && !nombreMinuscula.startsWith("fin_") && cantidadEncontrada < 20) {
        archivosEncontrados[cantidadEncontrada] = nombre;
        cantidadEncontrada++;
      }
    }
    archivo.close();
    archivo = raiz.openNextFile();
  }
  raiz.close();

  if (cantidadEncontrada == 0) {
    Serial.println("[INICIO] No hay pendientes anteriores");
    return;
  }
  Serial.println("[INICIO] Archivos pendientes encontrados");
  Serial.println(cantidadEncontrada);

  String nombrePendientesOriginal = nombreArchivoPendientes;

  for (int i = 0; i < cantidadEncontrada; i++) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[INICIO] Se perdió el wifi, detenemos por seguridad la sincronizacion!!");
      break;
    }
    nombreArchivoPendientes = archivosEncontrados[i];

    Serial.println("[INICIO] PROCESANDO:");
    Serial.println(nombreArchivoPendientes);
    sincronizarPendientes();
  }

  nombreArchivoPendientes = nombrePendientesOriginal;

  Serial.println("[INICIO] Revision de pendientes terminada");
}

// arma un JSON de finsesion con la hora del RTC y si tiene conexion le avisara al servidor que la sesion termino
// marca sesionIniciada como falso
void finalizarSesion() {
  DateTime ahora;

  if (hayFechaHoraValida) {
    ahora = ultimaFechaHoraValida;
  } else {
    ahora = rtc.now();
  }

  char fecha[11];
  char hora[9];
  sprintf(fecha, "%02d/%02d/%04d", ahora.day(), ahora.month(), ahora.year());
  sprintf(hora, "%02d:%02d:%02d", ahora.hour(), ahora.minute(), ahora.second());

  // solo si estamos en MODO_LABORATORIO notificamos/guardamos la trama de fin_sesion en web/pnd
  if (modoActual == MODO_LABORATORIO) {
    StaticJsonDocument<256> doc;
    doc["session_id"] = sessionId;
    doc["tipo"] = "fin_sesion";
    doc["fecha"] = fecha;
    doc["hora"] = hora;

    String body;
    serializeJson(doc, body);

    bool enviado = false;

    if (modoOnline) {
      int codigo = enviarMedicion(body);
      if (codigo >= 200 && codigo < 300) {
        Serial.println("[FIN] Servidor notificado correctamente");
        enviado = true;
      }
    }

    if (!enviado) {
      String nombreArchivoFin = "/FIN_" + sessionId + ".pnd";
      File f = SD.open(nombreArchivoFin, FILE_WRITE);
      if (f) {
        f.println(body);
        f.close();
        Serial.println("[FIN] Se almacenó el fin en la SD como pendiente a enviar: " + nombreArchivoFin);
      } else {
        Serial.println("[FIN] ERROR en el almacenado de archivo de cierre en SD.");
      }
    }
  } else {
    Serial.println("[FIN] Modo Campo: Cierre local guardado exclusivamente en CSV.");
  }

  sesionIniciada = false;

  Serial.println("==============================");
  Serial.println("✅ Sesion finalizada.");
  Serial.print("Total mediciones: ");
  Serial.println(medicionNum);
  Serial.println("==============================");
}

void ejecutarVentanaSincronizacionFinal() {
  // Si estamos en MODO CAMPO, ignoramos por completo el chequeo de pendientes y reconexión WiFi
  if (modoActual == MODO_LABORATORIO) {
    if (SD.exists("/" + nombreArchivoPendientes)) {
      Serial.println("[SYNC] Hay mediciones pendientes.");
      Serial.println("[SYNC] Esperando hasta 60 segundos para recuperar WiFi...");

      unsigned long inicioVentanaSync = millis();

      while (millis() - inicioVentanaSync < VENTANA_SYNC_FINAL) {
        gestionarConexion();
        if (modoOnline) {
          if (SD.exists("/" + nombreArchivoPendientes)) {
            Serial.println("[SYNC] Enviando mediciones de temperatura pendientes...");
            sincronizarPendientes();
          }

          Serial.println("[SYNC] Verificando cierres de sesión pendientes...");
          sincronizarCierresPendientes();

          if (!SD.exists("/" + nombreArchivoPendientes)) {
            Serial.println("[SYNC] ✅ Sincronización completa (datos y cierre al día).");
            break;
          }
        }
        delay(1000);
      }
      if (SD.exists("/" + nombreArchivoPendientes)) {
        Serial.println("[SYNC] ⚠️ No se pudieron enviar todas las pendientes");
        Serial.println("[SYNC] Se conservarán en la microSD");
      }
    }
  }

  if (sesionInterrumpidaPorError) {
    Serial.println("==============================");
    Serial.println("❌ Sesión interrumpida por error crítico.");
    Serial.print("Total mediciones: ");
    Serial.println(medicionNum);
    Serial.println("==============================");
  }

  finalizarSesion();
  limpiarSesion();
}

#endif