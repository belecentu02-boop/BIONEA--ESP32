//        L I B R E R I A S  G E N E R A L E S
#include <Preferences.h>
#include <max6675.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include "esp_system.h"
#include "esp_mac.h"
#include <WiFiManager.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

//        C O N F I G U R A C I Ó N
Preferences prefs;
WiFiManager wm;
WebServer server(80);

//        P I N E S
#define MAX6675_SCK 14
#define MAX6675_CS 17
#define MAX6675_SO 26
#define SD_CS 5
#define BOTON_PIN 4

//        O B J E T O S
Adafruit_PCD8544 display = Adafruit_PCD8544(15, 16, 32, 13, 27);
//                                                    ^^ DC ahora en GPIO32
MAX6675 termopar(MAX6675_SCK, MAX6675_CS, MAX6675_SO);
RTC_DS3231 rtc;

//        V A R I A B L E S  G L O B A L E S
bool sesionIniciada = false;
int minutosSesion = 0;
int intervaloSegundos = 35;
String deviceMAC = "";
String nombreAP = "";
bool modoOnline = false;
int medicionNum = 0;
float tempActual = 0;
String motivoError = "";

String nombreArchivoActual = "";
String nombreArchivoPendientes = "";

bool modoOnlineAnterior = false;
bool sesionInterrumpidaPorError = false;
const unsigned long VENTANA_SYNC_FINAL = 60000;

int erroresSensorConsecutivos = 0;
const int MAX_ERRORES_SENSOR = 3;

// VARIABLES DE SESIÓN
String sessionId = "";
String ultimoSessionIdProcesado = "";
String individuoCodigo = "";
String especieActual = "";

//variables para el BOTÓN
unsigned long tiempoPresionado = 0;
bool botonEstadoAnterior = HIGH; // HIGH por defecto gracias al INPUT_PULLUP
const unsigned long TIEMPO_PULSACION_LARGA = 2000; // 2 segundos
  // Lógica de Triple Pulsación (Cambio de Modo)
int contadorPulsaciones = 0;
unsigned long ultimoTiempoPulsacion = 0;
const unsigned long VENTANA_TRIPLE_CLIC = 1500; // 1.5 segundos para completar los 3 clics

//VARIABLES PARA EL FIN DE SESIÓN
DateTime ultimaFechaHoraValida(2000, 1, 1, 0, 0, 0);
bool hayFechaHoraValida = false;

unsigned long ultimoPoll = 0;
unsigned long ultimoIntentoWifi = 0;
unsigned long ultimoRefreshPantalla = 0; 
const unsigned long INTERVALO_REINTENTO_WIFI = 30000;  // 30 s entre intentos
const unsigned long POLL_SESION_MS = 30000;
const unsigned long INTERVALO_REFRESH_PANTALLA = 1000;

// E S T A D O S
enum EstadoBionea {
  INICIANDO,
  ESPERANDO_CONFIGURACION,
  SESION_PREPARADA,
  SESION_ACTIVA,
  SESION_FINALIZADA,
  SINCRONIZANDO,
  ERROR_CRITICO
};

enum ModoDispositivo { MODO_LABORATORIO, MODO_CAMPO };
ModoDispositivo modoActual = MODO_LABORATORIO;

EstadoBionea estadoActual = INICIANDO;

// P R O T O T I P O S   D E   F U N C I O N E S

bool inicializarCSV(String nombreArchivo);
bool guardarEnSD(String linea);
void guardarPendiente(String linea);
void limpiarSesion();
bool hayPendientes();
void actualizarPantallaEstado();
void mostrarPantallaReconectando();
void mostrarPantallaPortal();

// I N C L U S I Ó N   D E   M Ó D U L O S   L O C A L E S
#include "config_red.h" 
#include "modo_laboratorio.h"
#include "modo_campo.h"

// F U N C I O N E S  D E  H A R D W A R E

void guardarModoEnNVS(ModoDispositivo nuevoModo) {
  modoActual = nuevoModo;
  prefs.begin("bionea_cfg", false);
  prefs.putUInt("modo", (uint32_t)nuevoModo);
  prefs.end();
}

// FUNCIÓN PANTALLA

void inicializarPantalla() {
  display.begin();
  display.setContrast(55); // Ajustar según tu módulo (valores típicos entre 50 y 70)
  display.clearDisplay();
  display.display();
  display.setTextSize(1);
  display.setTextColor(BLACK);
}

String truncar(String s, int maxChars) {
  if (s.length() > maxChars) {
    return s.substring(0, maxChars);
  }
  return s;
}

void actualizarPantallaEstado() {
  display.clearDisplay();
  
  // 1. Cabecera: Modo y Estado de Red (WiFi / AP)
  display.setCursor(0, 0);
  if (modoActual == MODO_LABORATORIO) {
    display.print(modoOnline ? "LAB [WiFi]" : "LAB [OFF]");
  } else {
    display.print("CAMPO [AP]");
  }
  
  display.drawLine(0, 9, 84, 9, BLACK); // Línea divisoria

  // 2. Cuerpo dinámico según el estado del dispositivo
  display.setCursor(0, 12);
  switch (estadoActual) {
    case INICIANDO:
      display.print("Iniciando...");
      break;
      
    case ESPERANDO_CONFIGURACION: {
      display.setCursor(0, 12);
      display.print("Esperando web");
      display.setCursor(0, 24);
      display.print("MAC:");
      display.setCursor(0, 34);
      
      // Quitar los dos puntos y mostrar los 12 chars
      String macLimpia = deviceMAC;
      macLimpia.replace(":", "");   // "08D1F9ED9A40"
      display.print(macLimpia);
      } break;
      
    case SESION_PREPARADA: {
      display.print("Lista sesion");
      display.setCursor(0, 24);
      display.print(truncar("Ind: " + individuoCodigo, 14));
      } break;
      
    case SESION_ACTIVA: {
      // Mostrar individuo y temperatura actual en tiempo real
      display.print(truncar("Ind: " + individuoCodigo, 14));
      display.setCursor(0, 22);
      display.print("Temp: ");
      display.print(tempActual, 1);
      display.print(" C");
      
      // Mostrar límites y errores o estado de sensores
      display.setCursor(0, 32);
      display.print("Medicion #");
      display.print(medicionNum);
      } break;
      
    case SESION_FINALIZADA: {
      display.print("Sesion");
      display.setCursor(0, 24);
      display.print("Finalizada");
      } break;
      
    case SINCRONIZANDO: {
      display.print("Sincronizando");
      display.setCursor(0, 24);
      display.print("Subiendo datos");
      } break;
      
    case ERROR_CRITICO: {
      display.print("!ERROR!");
      display.setCursor(0, 24);
      display.print(motivoError);
      } break;
  }
  
  display.display(); // Refrescar la pantalla física
}

void mostrarPantallaReconectando() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);

  display.setCursor(0, 0);
  display.print("LAB [OFF]");
  display.drawLine(0, 9, 84, 9, BLACK);

  display.setCursor(0, 12);
  display.print("Reconectando");
  display.setCursor(0, 24);
  display.print("WiFi...");
  display.setCursor(0, 36);
  display.print("Espera 15s");

  display.display();
}

void mostrarPantallaPortal() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);

  display.setCursor(0, 0);
  display.print("LAB [OFF]");
  display.drawLine(0, 9, 84, 9, BLACK);

  display.setCursor(0, 12);
  display.print("Portal WiFi");

  // Mostrar nombre del AP (truncado)
  String ssid = "BIONEA-" + deviceMAC.substring(12);  // "BIONEA-ED:9A:40"
  ssid.replace(":", "");
  display.setCursor(0, 24);
  display.print(truncar(ssid, 14));

  display.setCursor(0, 36);
  display.print("192.168.4.1");

  display.display();
}

void errorCritico() {
  Serial.println("[ERROR] Estado critico alcanzado - bloqueando");
  estadoActual = ERROR_CRITICO;

  // Determinar el motivo UNA SOLA VEZ
  if (!rtcDisponible()) {
    motivoError = "Falta RTC";
  } else if (!sdDisponible()) {
    motivoError = "Falta MicroSD";
  } else {
    motivoError = "Hardware";
  }

  Serial.print("[ERROR] Motivo: ");
  Serial.println(motivoError);

  actualizarPantallaEstado();

  bool botonAnterior = HIGH;
  unsigned long ultimoRefresh = 0;

  while (true) {
    bool botonActual = digitalRead(BOTON_PIN);

    if (botonAnterior == HIGH && botonActual == LOW) {
      delay(50);
      if (digitalRead(BOTON_PIN) == LOW) {
        Serial.println("[ERROR] Reset manual solicitado por boton");
        delay(500);
        ESP.restart();
      }
    }
    botonAnterior = botonActual;

    if (millis() - ultimoRefresh >= 1000) {
      ultimoRefresh = millis();
      actualizarPantallaEstado();
    }
    delay(50);
  }
}

// FUNCIÓN BOTÓN
void gestionarBotonFisico() {
  bool estadoBoton = digitalRead(BOTON_PIN);

  // 1. Detectar cuando se PRESIONA el botón (Flanco de bajada)
  if (botonEstadoAnterior == HIGH && estadoBoton == LOW) {
    tiempoPresionado = millis();
    delay(50); // Antirrebote (debounce)
  }

  // 2. Detectar cuando se SUELTA el botón (Flanco de subida)
  if (botonEstadoAnterior == LOW && estadoBoton == HIGH) {
    unsigned long duracionPulsacion = millis() - tiempoPresionado;
    delay(50); // Antirrebote

    if (duracionPulsacion < 50) return; // Ignorar ruido / pulsaciones de microsegundos

    // --- EVENTO 1: PULSACIÓN LARGA (≥ 2 segundos) ---
    if (duracionPulsacion >= TIEMPO_PULSACION_LARGA) {
      contadorPulsaciones = 0; // Resetear la cuenta de clics cortos

      if (sesionIniciada) {
        Serial.println("[BOTON] Cierre manual de sesión activado.");
        sesionInterrumpidaPorError = false;
        sesionIniciada = false;
        estadoActual = SINCRONIZANDO;
      }
    } 
    // --- EVENTO 2: CLICS CORTOS (Para formar los 3 clics) ---
    else {
      unsigned long ahora = millis();

      // Si transcurrió más de 1.5s desde el último clic, se reinicia la cuenta
      if (ahora - ultimoTiempoPulsacion > VENTANA_TRIPLE_CLIC) {
        contadorPulsaciones = 1;
      } else {
        contadorPulsaciones++;
      }
      ultimoTiempoPulsacion = ahora;

      // Al completar los 3 clics rápidos (solo si no se está midiendo)
      if (contadorPulsaciones >= 3) {
        contadorPulsaciones = 0;

        if (!sesionIniciada) {
          if (modoActual == MODO_LABORATORIO) {
            Serial.println("[MODO] Cambiando a MODO CAMPO...");
            guardarModoEnNVS(MODO_CAMPO);
          } else {
            Serial.println("[MODO] Cambiando a MODO LABORATORIO...");
            guardarModoEnNVS(MODO_LABORATORIO);
          }

          delay(500);
          ESP.restart(); // Reiniciar para aplicar el cambio de modo y red
        }
      }
    }
  }

  botonEstadoAnterior = estadoBoton;
}

// RTC Y SD
bool rtcDisponible() {
  Wire.beginTransmission(0x68);
  byte error = Wire.endTransmission();

  return error == 0;
}

bool sdDisponible() {
  return SD.cardType() != CARD_NONE;
}

// FUNCIONES DE CSV Y SD
bool inicializarCSV(String nombreArchivo) {
  if (!SD.exists("/" + nombreArchivo)) {
    File archivo = SD.open("/" + nombreArchivo, FILE_WRITE);
    if (!archivo) {
      Serial.println("[SD] ❌ No se pudo crear el csv");
      return false;
    }
    archivo.println("ID_MEDICION,SESSION_ID,INDIVIDUO,ESPECIE,FECHA,HORA,TEMPERATURA");
    archivo.close();
  }
  return true;
}

bool guardarEnSD(String linea) {
  File archivo = SD.open("/" + nombreArchivoActual, FILE_APPEND);
  if (!archivo) {
    Serial.println("[SD] No se pudo abrir el csv");
    return false;
  }
  size_t bytesEscritos = archivo.println(linea);

  archivo.flush();
  archivo.close();

  if (bytesEscritos == 0) {
    Serial.println("[SD] No se pudo escribir la medicion");
    return false;
  }

  Serial.println("[SD] GUARDADO");
  return true;
}

void guardarPendiente(String linea) {
  bool archivoNuevo = !SD.exists("/" + nombreArchivoPendientes);
  File archivoPendientes = SD.open("/" + nombreArchivoPendientes, FILE_APPEND);

  if (!archivoPendientes) {
    Serial.println("No se pudo abrir el archivo de pendientes");
    return;
  }
  if (archivoNuevo) {
    archivoPendientes.println("ID_MEDICION,SESSION_ID,INDIVIDUO,ESPECIE,FECHA,HORA,TEMPERATURA");
  }

  archivoPendientes.println(linea);
  archivoPendientes.flush();
  archivoPendientes.close();

  Serial.print("[DEBUG PND] Archivo: /");
  Serial.println(nombreArchivoPendientes);

  Serial.print("[DEBUG PND] Existe despues de guardar: ");
  Serial.println(
    SD.exists("/" + nombreArchivoPendientes)
      ? "SI"
      : "NO"
  );

  Serial.println("[SD] ⚠️ Medición agregada a pendientes");
}

// EVITAR DUPLICACIÓN DE ENVIO DE DATOS

bool idYaEnviado(String idMedicion) {
  if (!SD.exists("/sent.log")) {
    return false;
  }

  File log = SD.open("/sent.log", FILE_READ);
  if (!log) {
    Serial.println("[SENT] No se pudo abrir sent.log");
    return false;
  }

  while (log.available()) {
    String linea = log.readStringUntil('\n');
    linea.trim();
    if (linea == idMedicion) {
      log.close();
      return true;
    }
  }

  log.close();
  return false;
}

void marcarComoEnviado(String idMedicion) {
  File log = SD.open("/sent.log", FILE_APPEND);
  if (!log) {
    Serial.println("[SENT] No se pudo abrir sent.log para escribir");
    return;
  }

  log.println(idMedicion);
  log.flush();
  log.close();

  Serial.print("[SENT] Marcado como enviado: ");
  Serial.println(idMedicion);
}

//

String generarIdMedicion(DateTime fecha, int numeroMedicion) {
  String macSinDosPuntos = deviceMAC;
  macSinDosPuntos.replace(":", "");

  char fechaId[20];

  sprintf(
    fechaId,
    "%04d%02d%02d%02d%02d%02d",
    fecha.year(),
    fecha.month(),
    fecha.day(),
    fecha.hour(),
    fecha.minute(),
    fecha.second());

  return macSinDosPuntos + "_" + String(fechaId) + "_" + String(numeroMedicion);
}

String armarLineaCSV(String idMedicion, String fecha, String hora, float temp) {
  return
    idMedicion + "," +
    sessionId + "," +
    individuoCodigo + "," +
    especieActual + "," +
    fecha + "," +
    hora + "," +
    String(temp, 2);
}

void limpiarSesion() {

  Serial.println("[SESION] Limpiando las variables...");
  
  sesionIniciada = false;
  sessionId = "";
  individuoCodigo = "";
  especieActual = "";

  minutosSesion = 0;
  intervaloSegundos = 35;

  medicionNum = 0;
  erroresSensorConsecutivos = 0;

  nombreArchivoActual = "";
  nombreArchivoPendientes = "";

  sesionInterrumpidaPorError = false;

  Serial.println("[SESION] Variables limpiadas");
  Serial.println("[SESION] Esp32 esperando nueva configuracion");
}

void ejecutarCicloSesion() {
  Serial.println("==============================");
  Serial.println("✅ Sesión iniciada!");
  Serial.print("MAC:        ");
  Serial.println(deviceMAC);
  Serial.print("Duración:   ");
  Serial.print(minutosSesion);
  Serial.println(" min");
  Serial.print("Intervalo:  ");
  Serial.print(intervaloSegundos);
  Serial.println(" seg");
  Serial.println("==============================");

  unsigned long duracion = (unsigned long)minutosSesion * 60 * 1000;
  unsigned long inicio = millis();

  while (millis() - inicio < duracion) {

    if (!sesionIniciada) {
      break;
    }

    unsigned long inicioMedicion = millis();

    if (!rtcDisponible()) {
      Serial.println("[ERROR CRITICO] RTC desconectado durante la sesion");
      errorCritico();
    }

    if (!sdDisponible()) {
      Serial.println("[ERROR CRITICO] MicroSD desconectada durante la sesion");
      errorCritico();
    }

    float temp = termopar.readCelsius();
    tempActual = temp;
    DateTime ahora = rtc.now();
    ultimaFechaHoraValida = ahora;
    hayFechaHoraValida = true;
    medicionNum++;

    char fechaHora[20];
    char fecha[11];
    char hora[9];
    sprintf(fechaHora,
            "%04d-%02d-%02d %02d:%02d:%02d",
            ahora.year(),
            ahora.month(),
            ahora.day(),
            ahora.hour(),
            ahora.minute(),
            ahora.second()
          );
    sprintf(
      fecha,
      "%02d/%02d/%04d",
      ahora.day(),
      ahora.month(),
      ahora.year()
    );

    sprintf(
      hora,
      "%02d:%02d:%02d",
      ahora.hour(),
      ahora.minute(),
      ahora.second()
    );
    String idMedicion =
      generarIdMedicion(ahora, medicionNum);

    bool lecturaInvalida = isnan(temp);

    //Cuando es una lectura inválida, hacemos...

    if (lecturaInvalida) {
      
      erroresSensorConsecutivos++;

      Serial.print("[SENSOR] Lectura invalida. Error ");
      Serial.print(erroresSensorConsecutivos);
      Serial.print("/");
      Serial.println(MAX_ERRORES_SENSOR);

      String lineaError =
        idMedicion + "," + sessionId + "," + individuoCodigo + "," + especieActual + "," + String(fecha) + "," + String(hora) + "ERROR_SENSOR";

      if (!guardarEnSD(lineaError)) {
        Serial.println("[ERROR CRITICO] No se pudo registrar el error del sensor");

        errorCritico();

        sesionInterrumpidaPorError = true;
        sesionIniciada = false;
        break;
      }

      if (erroresSensorConsecutivos >= MAX_ERRORES_SENSOR) {
        Serial.println("[ERROR] 3 lecturas invalidas consecutivas.");
        errorCritico();

        sesionInterrumpidaPorError = true;
        sesionIniciada = false;
        break;
      }
    }

    // Cuando es una lectura válida, hacemos...

    else {

      erroresSensorConsecutivos = 0;

      Serial.println("------------------------------");
      Serial.print("Medición #");
      Serial.println(medicionNum);
      Serial.print("Fecha/Hora:   ");
      Serial.println(fechaHora);
      Serial.print("Temperatura:  ");
      Serial.print(temp);
      Serial.println(" °C");
      Serial.println("------------------------------");

      String lineaCSV =
        armarLineaCSV(
          idMedicion,
          String(fecha),
          String(hora),
          temp
      );

      String lineaPendiente = lineaCSV;

      // El CSV principal siempre recibe la medición
      if (!guardarEnSD(lineaCSV)) {
        Serial.println("[ERROR] No se pudo guardar la medicion");
        errorCritico();

        sesionInterrumpidaPorError = true;
        sesionIniciada = false;
        break;
      }

      // y AHORA, el envío
      // Si MODO LABORATORIO -> intentamos enviar a la web o guardar las pendientes.
      // Si MODO CAMPO -> ignoramos completamente el envío y los pendientes.
      if (modoActual == MODO_LABORATORIO) {
        String body = armarJsonMedicion(idMedicion, String(fecha), String(hora), temp);
        int codigoRespuesta = 0;
        
        if (modoOnline) {
          codigoRespuesta = enviarMedicion(body);
        }

        bool envioActualExitoso = codigoRespuesta >= 200 && codigoRespuesta < 300;

        if (!modoOnline || !envioActualExitoso) {
          if (lineaPendiente != "") {
            guardarPendiente(lineaPendiente);
          }
        }
        else {
          marcarComoEnviado(idMedicion);
        }
        
        gestionarConexion();

        if (modoOnline && envioActualExitoso && SD.exists("/" + nombreArchivoPendientes)) {
          sincronizarPendientes();
        }
      }
    }

    actualizarPantallaEstado();
    ultimoRefreshPantalla = millis();
    // Ahora, viene la espera...
    unsigned long tiempoUsado = millis() - inicioMedicion;

    unsigned long intervaloMs = (unsigned long)intervaloSegundos * 1000UL;

    if (tiempoUsado < intervaloMs) {
      unsigned long tiempoRestante = intervaloMs - tiempoUsado;
      unsigned long inicioEspera = millis();

      while (millis() - inicioEspera < tiempoRestante) {
        //mientras estamos al pedo, intentamos revisar el wifi
        gestionarConexion();
        gestionarBotonFisico();

        if (!sesionIniciada) {
          break;
        }

        if (!rtcDisponible()) {
          Serial.println("[ERROR CRITICO] RTC desconectado durante la sesion");
          errorCritico();
        }

        if (!sdDisponible()) {
          Serial.println("[ERROR CRITICO] MicroSD desconectada durante la sesion");
          errorCritico();
        }

        if (millis() - ultimoRefreshPantalla >= INTERVALO_REFRESH_PANTALLA) {
          ultimoRefreshPantalla = millis();
          actualizarPantallaEstado();
        }

        // finalizar si ya se cumplio el tiempo
        unsigned long transcurrido = millis() - inicioEspera;
        if (transcurrido >= tiempoRestante) break;
        unsigned long falta = tiempoRestante - transcurrido;

        if (falta > 1000) {
          delay(1000);
        } else {
          delay(falta);
        }
      }
    }
  }
  estadoActual = SINCRONIZANDO;
}


// ==============================
//           SETUP
// ==============================
void setup() {
  Serial.begin(115200);
  prefs.begin("bionea_cfg", false);
  modoActual = (ModoDispositivo)prefs.getUInt("modo", MODO_LABORATORIO);
  prefs.end();

  //Para no agarrar una sesión que ya se midió
  prefs.begin("bionea_cfg", true);
  ultimoSessionIdProcesado = prefs.getString("ultima_sesion", "");
  prefs.end();

  Serial.print("[NVS] Ultima sesion procesada: ");
  Serial.println(ultimoSessionIdProcesado);

  //wm.resetSettings();
  pinMode(BOTON_PIN, INPUT_PULLUP);
  // 1. Inicializamos pantalla:
  inicializarPantalla();
  Serial.println("Pantalla OK");

  // 2. Inicializamos RTC
  Wire.begin(21, 22);
  if (!rtc.begin()) {
    Serial.println("❌ RTC no encontrado");
    errorCritico();
  }
  Serial.println("RTC OK");

  // 3. Inicializamos MicroSD
  pinMode(MAX6675_CS, OUTPUT);
  digitalWrite(MAX6675_CS, HIGH);
  SPI.begin(18, 19, 23, 5);
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ MicroSD no encontrada");
    errorCritico();
  }
  Serial.println("MicroSD OK");

  if (SD.exists("/TEMP.PND")) {
    Serial.println("[SD] Detectado TEMP.PND huerfano, borrando...");
    if (SD.remove("/TEMP.PND")) {
      Serial.println("[SD] TEMP.PND borrado");
    }
    else {
      Serial.println("[SD] NO SE PUDO BORRAR TEMP.PND");
    }
  }


  // WiFi
  uint8_t macBytes[6];
  esp_read_mac(macBytes, ESP_MAC_WIFI_STA);

  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          macBytes[0], macBytes[1], macBytes[2],
          macBytes[3], macBytes[4], macBytes[5]);
  deviceMAC = String(macStr);
  Serial.println("MAC del dispositivo: " + deviceMAC);

  nombreAP = "BIONEA-" + deviceMAC;
  nombreAP.replace(":", "");

  //SECCIÓN DE CONDICION ENTRE CAMPOS DE TRABAJO :)
  if (modoActual == MODO_CAMPO) {
    Serial.println("[MODO] Arrancando en MODO CAMPO (Offline AP)");
    iniciarAPModoCampo();
  }
  else {
    Serial.println("[MODO] Arrancando en MODO LABORATORIO (WiFiManager)");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin();

    // "CONECTANDO..."
    unsigned long inicioIntento = millis();
    unsigned long ultimoRefreshWifi = 0;

    while (WiFi.status() != WL_CONNECTED && millis() - inicioIntento < 15000) {
      if (millis() - ultimoRefreshWifi >= 500) {
        ultimoRefreshWifi = millis();
        int segundosRestantes = (15000 - (millis() - inicioIntento)) / 1000;

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(BLACK);

        display.setCursor(0, 0);
        display.print("LAB");
        display.drawLine(0, 9, 84, 9, BLACK);

        display.setCursor(0, 12);
        display.print("Conectando red");
        display.setCursor(0, 24);
        display.print("Esperando...");
        display.setCursor(0, 36);
        display.print(segundosRestantes);
        display.print("s");

        display.display();
      }
      delay(50);
    }

    modoOnline = (WiFi.status() == WL_CONNECTED);

    if (modoOnline) {
      Serial.println("[WIFI] Conectado al arrancar");
      sincronizarPendientesAnteriores();
      sincronizarCierresPendientes();
    } 
    else {
      Serial.println("[WIFI] Sin conexion al arrancar. Se reintentara en el loop.");
    }
  
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    modoOnlineAnterior = modoOnline;
  }

  actualizarPantallaEstado();
}

// ==============================
//           LOOP
// ==============================
void loop() {
  // Actualización constante de periféricos visuales
  static unsigned long ultimoRefreshGlobal = 0;
  if (millis() - ultimoRefreshGlobal >= INTERVALO_REFRESH_PANTALLA) {
    ultimoRefreshGlobal = millis();
    actualizarPantallaEstado();
  }

  // Máquina de estados central
  switch (estadoActual) {
    case INICIANDO:
      if (!rtcDisponible() || !sdDisponible()) {
        errorCritico();
      }
      // Verificaciones iniciales superadas, pasa a la escucha o configuración
      estadoActual = ESPERANDO_CONFIGURACION;
      actualizarPantallaEstado();
      break;

    case ESPERANDO_CONFIGURACION: {
      // Aquí opera el servidor web local (AP Mode) o la espera de órdenes del dashboard
      gestionarBotonFisico();
      server.handleClient();

      if (!rtcDisponible()) {
        Serial.println("[ERROR CRITICO] RTC desconectado en espera");
        errorCritico();   // ← esto ya bloquea en while(true)
      }
      if (!sdDisponible()) {
        Serial.println("[ERROR CRITICO] SD desconectada en espera");
        errorCritico();
      }

      bool wifiReal = (WiFi.status() == WL_CONNECTED);
      if (wifiReal != modoOnline) {
        modoOnline = wifiReal;
        if (modoOnline) {
          Serial.println("[WIFI] Recuperado (detectado en loop)");
          sincronizarPendientesAnteriores();
          sincronizarCierresPendientes();
        } else {
          Serial.println("[WIFI] Perdido (detectado en loop)");
        }
        actualizarPantallaEstado();
      }
      
      if (sesionIniciada) {
        estadoActual = SESION_PREPARADA;
        actualizarPantallaEstado();
        break;
      }

      // CICLO PARA RECONEXIÓN -----
      if (modoActual == MODO_LABORATORIO && !modoOnline) {
        if (millis() - ultimoIntentoWifi >= INTERVALO_REINTENTO_WIFI) {
          ultimoIntentoWifi = millis();
          Serial.println("[WIFI] Intento de reconexión...");

          mostrarPantallaReconectando();

          WiFi.begin();
          unsigned long inicioIntento = millis();
          while (WiFi.status() != WL_CONNECTED && millis() - inicioIntento < 15000) {
            gestionarBotonFisico();
            delay(50);
          }

          if (WiFi.status() == WL_CONNECTED) {
            modoOnline = true;
            Serial.println("[WIFI] Reconectado");
            sincronizarPendientesAnteriores();
            sincronizarCierresPendientes();
            actualizarPantallaEstado();
          }
          else {
            Serial.println("[WIFI] Falla. Abriendo portal de 3 min...");

            mostrarPantallaPortal();
            wm.setConfigPortalTimeout(180);
            wm.startConfigPortal(nombreAP.c_str());
            modoOnline = (WiFi.status() == WL_CONNECTED);
            Serial.println(modoOnline ? "[WIFI] Conectado desde portal" : "[WIFI] Portal cerrado sin conexion");
            actualizarPantallaEstado();
          }
        }
      }
      // Si estamos online -----
      if (modoOnline && millis() - ultimoPoll >= POLL_SESION_MS) {
        ultimoPoll = millis();
        consultarSesionAsignada();
        if (sesionIniciada) {
          estadoActual = SESION_PREPARADA;
          actualizarPantallaEstado();
        }
      }
    } break;

    case SESION_PREPARADA:
      if (!rtcDisponible() || !sdDisponible()) {
        errorCritico();
      }
      // Todo listo para registrar temperatura de los lagartos en campo
      delay(1000); // Breve pausa de confirmación visual
      estadoActual = SESION_ACTIVA;
      actualizarPantallaEstado();
      break;

    case SESION_ACTIVA: 
      // Lógica de muestreo de temperatura, registro en SD y control de failover
      ejecutarCicloSesion(); 
      // Al finalizar la sesión de campo (por tiempo o botón), transiciona:
      if (!sesionIniciada) {
        estadoActual = SINCRONIZANDO;
        actualizarPantallaEstado();
      }
      break;

    case SINCRONIZANDO:
      // Volcado automático de datos desde la SD hacia Firebase al detectar WiFi
      if (!rtcDisponible() || !sdDisponible()) {
        errorCritico();
      }

      ejecutarVentanaSincronizacionFinal();
      estadoActual = ESPERANDO_CONFIGURACION;
      actualizarPantallaEstado();
      break;
      
    case SESION_FINALIZADA:
      // Reseteo de banderas y preparación para el siguiente ciclo
      estadoActual = ESPERANDO_CONFIGURACION;
      actualizarPantallaEstado();
      break;

    case ERROR_CRITICO:
      errorCritico();
      break;
  }
}
/////
