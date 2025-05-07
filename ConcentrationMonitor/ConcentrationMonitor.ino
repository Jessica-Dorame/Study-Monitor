/*
 * ConcentrationMonitor.ino
 * Sistema de monitoreo de "Horas de concentración"
 * Conexión WiFi, servidor web Async, sensores y MySQL directo
 *
 * - Web interface: index.html, monitor.html, resultados.html
 * - Lectura sensores: DS18B20 (temp), LDR (luz), mic (sonido), PIR (mov)
 * - Endpoint JSON /datos para tiempo real
 * - Al finalizar sesión, inserta resumen en MySQL con NOW()
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <time.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NoDelay.h>
#include <ArduinoJson.h>
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>

// --- Configuración WiFi ---
const char* SSID     = "INFINITUM0382_2.4";
const char* PASSWORD = "2ERMhmD5t0";

// --- Configuración MySQL ---
IPAddress MYSQL_SERVER(192,168,1,50); // IP del servidor MySQL
char MYSQL_USER[] = "root";
char MYSQL_PASS[] = "jorgendo43";
char MYSQL_DB[]   = "concentracion";

WiFiClient      wifiClient;
MySQL_Connection mysqlConn(&wifiClient);
MySQL_Cursor    mysqlCur(&mysqlConn);

// --- Servidor web ---
AsyncWebServer server(80);

// --- Pines sensores ---
#define PIN_DS18B20      4   // OneWire bus
#define PIN_LDR         36   // Luz
#define PIN_MIC         12   // Sonido
#define PIN_PIR         27   // Movimiento

OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);
DeviceAddress tempAddr;

// --- Intervalo lectura sensores ---
const long INTERVALO_SENSADO = 1000;
noDelay sensadoTimer(INTERVALO_SENSADO);

// --- Variables sesión y métricas ---
String nombreUsuario;
String nombreMateria;
unsigned long tiempoDeseado  = 0; // ms
unsigned long tiempoInicio   = 0;
unsigned long tiempoRestante = 0;
bool sesionActiva            = false;

float temperaturaActual      = 0;
int luzActual                = 0;
int sonidoActual             = 0;
bool movimientoActual        = false;
int contadorMovimientos      = 0;
int contadorSonidos          = 0;
int totalDistracciones       = 0;
int totalLecturas            = 0;
float sumaTemperatura        = 0;
long sumaLuz                 = 0;
long sumaSonido              = 0;
float promedioTemperatura    = 0;
float promedioLuz            = 0;
int promedioSonido           = 0;
struct tm timeinfo;
String fechaHoraActual;

// --- Prototipos ---
String processor(const String& var);
void conectarWiFi();
void initLittleFS();
void initTempSensor();
void conectarMySQL();
void leerSensores();
void actualizarTIempo();
void iniciarSesion(const String& nombre, const String& materia, unsigned long tiempoMs);
void finalizarSesion();
void configurarServidor();

// --- Procesador HTML ---
String processor(const String& var) {
  if(var=="NOMBRE") return nombreUsuario;
  if(var=="MATERIA") return nombreMateria;
  if(var=="TIEMPO_DESEADO") return String(tiempoDeseado/60000);
  if(var=="TIEMPO_RESTANTE") return String(tiempoRestante/1000);
  if(var=="FECHA_HORA") return fechaHoraActual;
  if(var=="DISTRACCIONES") return String(totalDistracciones);
  if(var=="DISTRACCIONES_MOVIMIENTO") return String(contadorMovimientos);
  if(var=="DISTRACCIONES_SONIDO") return String(contadorSonidos);
  if(var=="LUZ_PROMEDIO") return String(promedioLuz);
  if(var=="TEMPERATURA_PROMEDIO") return String(promedioTemperatura);
  if(var=="RUIDO_PROMEDIO") return String(promedioSonido);
  return String();
}

void setup() {
  Serial.begin(115200); delay(500);
  Serial.println("=== Iniciando ConcentrationMonitor ===");

  conectarWiFi();
  initLittleFS();
  initTempSensor();
  conectarMySQL();
  configurarServidor();

  server.begin();
  Serial.println("Servidor web iniciado");

  // LEDs iniciales
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  if(sesionActiva && sensadoTimer.update()) {
    leerSensores();
    actualizarTIempo();
  }
}

// --- Funciones ---
void conectarWiFi() {
  Serial.print("Conectando WiFi...");
  WiFi.begin(SSID, PASSWORD);
  while(WiFi.status()!=WL_CONNECTED) { delay(500); Serial.print('.'); }
  Serial.println();
  Serial.print("IP: "); Serial.println(WiFi.localIP());
}

void initLittleFS() {
  if(!LittleFS.begin(true)) Serial.println("Error montando LittleFS");
  else Serial.println("LittleFS OK");
}

void initTempSensor() {
  sensors.begin();
  if(sensors.getAddress(tempAddr,0)) {
    sensors.setResolution(tempAddr,12);
    Serial.println("DS18B20 listo");
  } else Serial.println("DS18B20 no encontrado");
}

void conectarMySQL() {
  Serial.print("Conectando MySQL...");
  if(mysqlConn.connect(MYSQL_SERVER,3306,MYSQL_USER,MYSQL_PASS)) {
    Serial.println("Conectado a MySQL");
  } else Serial.println("Error MySQL");
}

void leerSensores() {
  // Temp
  sensors.requestTemperatures();
  temperaturaActual = sensors.getTempC(tempAddr);
  sumaTemperatura += temperaturaActual;
  Serial.print("Temp: "); Serial.println(temperaturaActual);
  // Luz
  luzActual = analogRead(PIN_LDR);
  sumaLuz += luzActual;
  Serial.print("Luz: "); Serial.println(luzActual);
  // Sonido
  sonidoActual = analogRead(PIN_MIC);
  sumaSonido += sonidoActual;
  Serial.print("Sonido: "); Serial.println(sonidoActual);
  // Movimiento
  movimientoActual = digitalRead(PIN_PIR);
  if(movimientoActual){ contadorMovimientos++; totalDistracciones++; Serial.println("Movimiento!"); }
  if(sonidoActual>500){ contadorSonidos++; totalDistracciones++; Serial.println("Ruido!"); }
  totalLecturas++;
}

void actualizarTIempo() {
  unsigned long dt = millis() - tiempoInicio;
  if(dt >= tiempoDeseado) {
    finalizarSesion();
  } else {
    tiempoRestante = tiempoDeseado - dt;
    Serial.print("Tiempo restante s: "); Serial.println(tiempoRestante/1000);
  }
}

void iniciarSesion(const String& nombre,const String& materia,unsigned long tiempoMs) {
  nombreUsuario=nombre; nombreMateria=materia;
  tiempoDeseado=tiempoMs; tiempoInicio=millis(); sesionActiva=true;
  tiempoRestante=tiempoDeseado;
  contadorMovimientos=contadorSonidos=totalDistracciones=totalLecturas=0;
  sumaTemperatura=sumaLuz=sumaSonido=0;
  Serial.printf("Sesion iniciada: %s - %s, %lums\n",nombre.c_str(),materia.c_str(),tiempoMs);
}

void finalizarSesion() {
  sesionActiva=false;
  // Calcular promedios
  if(totalLecturas>0) {
    promedioLuz=sumaLuz/(float)totalLecturas;
    promedioTemperatura=sumaTemperatura/(float)totalLecturas;
    promedioSonido=sumaSonido/totalLecturas;
  }
  // Fecha y hora
  if(getLocalTime(&timeinfo)){
    char buf[64]; strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&timeinfo);
    fechaHoraActual=buf;
  }
  Serial.println("Sesión finalizada");
  // Insertar en MySQL
  char q[256];
  snprintf(q,sizeof(q),
    "INSERT INTO Sesion (Usuario, Materia, DuracionDeseada, Distracciones, LuzPromedio, TemperaturaPromedio, RuidoPromedio, FechaHora) "
    "VALUES ('%s','%s',%lu,%d,%.1f,%.1f,%d,NOW());",
    nombreUsuario.c_str(),nombreMateria.c_str(),tiempoDeseado,totalDistracciones,
    promedioLuz,promedioTemperatura,promedioSonido);
  mysqlCur.execute(q);
  Serial.print("SQL: "); Serial.println(q);
}

void configurarServidor() {
  // Rutas dinámicas
  server.on("/", HTTP_GET,[](AsyncWebServerRequest* r){ r->send(LittleFS,"/index.html","text/html",false,processor); });
  server.on("/monitor", HTTP_GET,[](AsyncWebServerRequest* r){ r->send(LittleFS,"/monitor.html","text/html",false,processor); });
  server.on("/resultados", HTTP_GET,[](AsyncWebServerRequest* r){ r->send(LittleFS,"/resultados.html","text/html",false,processor); });
  // Iniciar
  server.on("/iniciar", HTTP_POST,[](AsyncWebServerRequest* req){
    String nom=req->getParam("nombre",true)->value();
    String mat=req->getParam("materia",true)->value();
    unsigned long t=req->getParam("tiempo",true)->value().toInt()*60000;
    iniciarSesion(nom,mat,t);
    req->redirect("/monitor");
  });
  // Detener
  server.on("/detener", HTTP_POST,[](AsyncWebServerRequest* req){
    if(sesionActiva) finalizarSesion();
    req->redirect("/resultados");
  });
  // JSON datos
  server.on("/datos", HTTP_GET,[](AsyncWebServerRequest* req){
    if(sesionActiva){ leerSensores(); actualizarTIempo(); }
    DynamicJsonDocument doc(256);
    doc["sesionActiva"]=sesionActiva;
    doc["tiempoRestante"]=tiempoRestante;
    doc["temperatura"]=temperaturaActual;
    doc["luz"]=luzActual;
    doc["sonido"]=sonidoActual;
    doc["movimiento"]=movimientoActual;
    doc["totalDistracciones"]=totalDistracciones;
    String out; serializeJson(doc,out);
    req->send(200,"application/json",out);
  });
  // Assets
  server.serveStatic("/styles.css",LittleFS,"/styles.css");
  server.serveStatic("/script.js",LittleFS,"/script.js");
  server.serveStatic("/images",LittleFS,"/images");
}
