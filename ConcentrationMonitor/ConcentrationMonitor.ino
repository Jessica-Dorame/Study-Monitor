#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include <LittleFS.h>
#include "time.h"
#include "DHT.h"
#include <NoDelay.h>
#include <ArduinoJson.h>

// --- Configuración WiFi ---
const char* ssid     = "INFINITUM0382_2.4";
const char* password = "2ERMhmD5t0";

// --- Pines y sensores ---
#define LED_VERDE         2
#define LED_ROJO         26
#define BUZZER           23
#define SENSOR_MOVIMIENTO 27
#define SENSOR_LUZ        36
#define SENSOR_SONIDO     12
#define DHTPIN            4
#define DHTTYPE           DHT11

// --- Hora NTP ---
const char* ntpServer       = "pool.ntp.org";
const long gmtOffset_sec    = -21600;
const int  daylightOffset_sec = 0;

// --- Intervalos ---
const long INTERVALO_LECTURA = 1000;
noDelay   lecturaInterval(INTERVALO_LECTURA);

// --- Variables de sesión ---
bool            sesionActiva    = false;
String          nombreUsuario;
String          nombreMateria;
unsigned long   tiempoDeseado   = 0;  // en segundos
unsigned long   tiempoInicio    = 0;
unsigned long   tiempoRestante  = 0;

// --- Estadísticas ---
int   contadorMovimientos = 0;
int   contadorSonidos     = 0;
int   totalDistracciones  = 0;
int   totalLecturas       = 0;
int   sumaLuz             = 0;
int   sumaSonido          = 0;
float sumaTemperatura     = 0;

// --- Lecturas actuales ---
float temperaturaActual = 0;
int   luzActual         = 0;
int   sonidoActual      = 0;
bool  movimientoActual  = false;

// Sensor DHT
DHT dht(DHTPIN, DHTTYPE);

// Servidor async
AsyncWebServer server(80);

void conectarWiFi();
void inicializarFS();
void configurarServidor();
void iniciarSesion();
void finalizarSesion();
void leerSensores();

void setup() {
  Serial.begin(115200);
  // Pines
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(SENSOR_MOVIMIENTO, INPUT);
  pinMode(SENSOR_LUZ, INPUT);
  pinMode(SENSOR_SONIDO, INPUT);

  dht.begin();
  inicializarFS();
  conectarWiFi();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  configurarServidor();
  server.begin();

  // Estado inicial
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_ROJO, HIGH);
}

void loop() {
  if (lecturaInterval.update() && sesionActiva) {
    leerSensores();
    // El cálculo de tiempo restante se hace justo antes de enviar /datos
  }
}

void conectarWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP asignada: ");
  Serial.println(WiFi.localIP());   // <— Aquí
}

void inicializarFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("Error montando LittleFS");
  }
}

void configurarServidor() {
  // Sirve index.html en /
  server.serveStatic("/", LittleFS, "/index.html")
        .setDefaultFile("index.html");

  // POST /iniciar
  server.on("/iniciar", HTTP_POST, [](AsyncWebServerRequest *req){
    if (req->hasParam("nombre", true) &&
        req->hasParam("materia", true) &&
        req->hasParam("tiempo", true)) {

      nombreUsuario = req->getParam("nombre", true)->value();
      nombreMateria = req->getParam("materia", true)->value();
      tiempoDeseado = req->getParam("tiempo", true)
                           ->value().toInt() * 60; // en s

      iniciarSesion();
      // 303: redirección suave, el cliente JS no recarga
      req->send(303, "text/plain", "");
    } else {
      req->send(400, "text/plain", "Faltan parámetros");
    }
  });

  // GET /datos → JSON con estado
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *req){
    DynamicJsonDocument doc(256);
    if (sesionActiva) {
      unsigned long elapsed = (millis() - tiempoInicio) / 1000;
      if (elapsed >= tiempoDeseado) {
        tiempoRestante = 0;
        finalizarSesion();
      } else {
        tiempoRestante = tiempoDeseado - elapsed;
      }
    }
    doc["sesionActiva"]   = sesionActiva;
    doc["tiempoRestante"] = tiempoRestante;
    doc["temperatura"]    = temperaturaActual;
    doc["luz"]            = luzActual;
    doc["distracciones"]  = totalDistracciones;

    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json);
  });

  server.onNotFound([](AsyncWebServerRequest *req){
    req->send(404, "text/plain", "No encontrado");
  });
}

void iniciarSesion() {
  sesionActiva    = true;
  tiempoInicio    = millis();
  totalLecturas   = sumaLuz = sumaSonido = sumaTemperatura = 0;
  contadorMovimientos = contadorSonidos = totalDistracciones = 0;

  digitalWrite(LED_VERDE, HIGH);
  digitalWrite(LED_ROJO, LOW);
  tone(BUZZER, 1000, 300);
}

void finalizarSesion() {
  sesionActiva = false;
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_ROJO, HIGH);
  tone(BUZZER, 2000, 800);
}

void leerSensores() {
  // Temperatura
  float t = dht.readTemperature();
  if (!isnan(t)) {
    temperaturaActual  = t;
    sumaTemperatura   += t;
  }
  // Luz y sonido
  luzActual    = analogRead(SENSOR_LUZ);
  sonidoActual = analogRead(SENSOR_SONIDO);
  sumaLuz     += luzActual;
  sumaSonido  += sonidoActual;

  // Movimiento
  movimientoActual = digitalRead(SENSOR_MOVIMIENTO);
  if (movimientoActual) {
    contadorMovimientos++;
    totalDistracciones++;
  }
  if (sonidoActual > 500) {
    contadorSonidos++;
    totalDistracciones++;
  }
  totalLecturas++;
}
