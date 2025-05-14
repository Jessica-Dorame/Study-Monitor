/*
 * ConcentrationMonitor.ino
 *
 * Sistema de monitoreo de "Horas de concentración" para ayudar a los
 * usuarios a medir sus sesiones de estudio.
 * 
 * Características:
 * - Interfaz web para ingresar nombre, materia y tiempo de concentración
 * - Monitoreo de condiciones ambientales (luz, temperatura, movimiento, sonido)
 * - Indicadores LED y alarmas para sesiones de concentración
 * - Almacenamiento de datos en base de datos externa (vía comunicación serial)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include <LittleFS.h>
#include "time.h"
#include "DHT.h"
#include <NoDelay.h>
#include <ArduinoJson.h>

// Configuración de WiFi
const char* ssid = "INFINITUM0382_2.4";
const char* password = "2ERMhmD5t0";

// Configuración de pines
#define LED_VERDE 2
#define LED_ROJO 26
#define BUZZER 23
#define SENSOR_MOVIMIENTO 27
#define SENSOR_LUZ 36
#define SENSOR_SONIDO 12
#define DHTPIN 4
#define DHTTYPE DHT11

typedef long timezone_t;
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -21600;
const int daylightOffset_sec = 0;

// Variables de tiempo
const long INTERVALO_LECTURA = 1000;

// Instancia del sensor de temperatura DHT
DHT dht(DHTPIN, DHTTYPE);

// Instancia del servidor web en puerto 80
AsyncWebServer server(80);

// Instancia para manejo de intervalos de tiempo
noDelay lecturaInterval(INTERVALO_LECTURA);
// Variables para almacenar datos
struct tm timeinfo;
String fechaHoraActual;
String nombreUsuario = "";
String nombreMateria = "";
unsigned long tiempoDeseado = 0;
unsigned long tiempoInicio = 0;
unsigned long tiempoRestante = 0;  // Corregido - eliminado \( extra
bool sesionActiva = false;
int contadorMovimientos = 0;
int contadorSonidos = 0;
int totalDistracciones = 0;
float promedioLuz = 0;
float promedioTemperatura = 0;
int promedioSonido = 0;
int totalLecturas = 0;
int sumaLuz = 0;
int sumaSonido = 0;
float sumaTemperatura = 0;

// Variables para datos en tiempo real
float temperaturaActual = 0;
int luzActual = 0;
int sonidoActual = 0;
bool movimientoActual = false;

// Función para conectar a WiFi
void conectarWiFi() {
  Serial.println("Conectando a WiFi...");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi conectado!");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());  // <- Aquí ya mostrabas la IP
}


// Función para inicializar LittleFS
void inicializarLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("Error al montar LittleFS");
    return;
  }
  Serial.println("LittleFS montado correctamente");
}

// Función para configurar el servidor web
void configurarServidor() {
  // Rutas para archivos estáticos
  server.serveStatic("/", LittleFS, "/");
  
  // Página de inicio - Configuración de sesión
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html", false, processor);
  });
  
  // Página de monitoreo de tiempo
  server.on("/monitor", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/monitor.html", "text/html", false, processor);
  });
  
  // Página de resultados
  server.on("/resultados", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/resultados.html", "text/html", false, processor);
  });
  
  // Endpoint para iniciar sesión
  server.on("/iniciar", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("nombre", true) && 
        request->hasParam("materia", true) && 
        request->hasParam("tiempo", true)) {
      
      nombreUsuario = request->getParam("nombre", true)->value();
      nombreMateria = request->getParam("materia", true)->value();
      tiempoDeseado = request->getParam("tiempo", true)->value().toInt() * 60 * 1000; // Convertir minutos a milisegundos
      
      Serial.println("Iniciando sesión con los siguientes datos:");
      Serial.println("Nombre: " + nombreUsuario);
      Serial.println("Materia: " + nombreMateria);
      Serial.println("Tiempo (ms): " + String(tiempoDeseado));
      
      // Iniciar sesión
      iniciarSesion();
      
      // Redireccionar a página de monitoreo
      request->redirect("/monitor");
    } else {
      request->send(400, "text/plain", "Faltan parámetros");
    }
  });
  
  // Endpoint para detener sesión
  server.on("/detener", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (sesionActiva) {
      finalizarSesion();
    }
    request->redirect("/resultados");
  });
  
  // Endpoint para obtener datos actuales en formato JSON
  server.on("/datos", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument doc(1024);
    
    // Asegurar que el tiempo restante se envía correctamente
    if (sesionActiva) {
      // Calcular el tiempo restante actualizado
      unsigned long tiempoTranscurrido = millis() - tiempoInicio;
      if (tiempoTranscurrido >= tiempoDeseado) {
        tiempoRestante = 0;
      } else {
        tiempoRestante = tiempoDeseado - tiempoTranscurrido;
      }
    }
    
    doc["tiempoRestante"] = tiempoRestante; // Enviar en milisegundos
    doc["sesionActiva"] = sesionActiva;
    doc["temperatura"] = temperaturaActual;
    doc["luz"] = luzActual;
    doc["sonido"] = sonidoActual;
    doc["movimiento"] = movimientoActual;
    doc["contadorMovimientos"] = contadorMovimientos;
    doc["contadorSonidos"] = contadorSonidos;
    doc["totalDistracciones"] = totalDistracciones;
    
    // Imprimir los datos que se están enviando para depuración
    Serial.println("Enviando datos al cliente:");
    Serial.print("Tiempo restante: ");
    Serial.println(tiempoRestante);
    Serial.print("Sesión activa: ");
    Serial.println(sesionActiva ? "Sí" : "No");
    Serial.print("Temperatura: ");
    Serial.println(temperaturaActual);
    Serial.print("Luz: ");
    Serial.println(luzActual);
    Serial.print("Sonido: ");
    Serial.println(sonidoActual);
    Serial.print("Movimiento: ");
    Serial.println(movimientoActual ? "Sí" : "No");
    
    serializeJson(doc, *response);
    request->send(response);
  });
  
  // Manejo de rutas no encontradas
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Página no encontrada");
  });
}

// Función para iniciar la sesión de concentración
void iniciarSesion() {
  // Establecer bandera de sesión activa
  sesionActiva = true;
  
  // Encender LED verde, apagar LED rojo
  digitalWrite(LED_VERDE, HIGH);
  digitalWrite(LED_ROJO, LOW);
  
  // Reproducir sonido de inicio
  tone(BUZZER, 1000, 500);
  
  // Resetear variables de conteo
  contadorMovimientos = 0;
  contadorSonidos = 0;
  totalDistracciones = 0;
  totalLecturas = 0;
  sumaLuz = 0;
  sumaSonido = 0;
  sumaTemperatura = 0;
  
  // Marcar tiempo de inicio
  tiempoInicio = millis();
  tiempoRestante = tiempoDeseado; // Inicializar tiempo restante
  
  // Mostrar en consola
  Serial.println("Sesión iniciada");
  Serial.print("Usuario: ");
  Serial.println(nombreUsuario);
  Serial.print("Materia: ");
  Serial.println(nombreMateria);
  Serial.print("Tiempo deseado (minutos): ");
  Serial.println(tiempoDeseado / 60000);
}

// Función para finalizar la sesión de concentración
void finalizarSesion() {
  // Establecer bandera de sesión inactiva
  sesionActiva = false;
  
  // Apagar LED verde, encender LED rojo
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_ROJO, HIGH);
  
  // Reproducir sonido de finalización
  tone(BUZZER, 2000, 1000);
  
  // Calcular promedios
  if (totalLecturas > 0) {
    promedioLuz = sumaLuz / (float)totalLecturas;
    promedioTemperatura = sumaTemperatura / (float)totalLecturas;
    promedioSonido = sumaSonido / (float)totalLecturas;
  }
  
  // Calcular tiempo total de la sesión
  unsigned long tiempoTotal = millis() - tiempoInicio;
  
  // Obtener fecha y hora actual
  if (getLocalTime(&timeinfo)) {
    char fechaHora[64];
    strftime(fechaHora, sizeof(fechaHora), "%Y-%m-%d %H:%M:%S", &timeinfo);
    fechaHoraActual = String(fechaHora);
  }
  
  // Enviar datos a Python para almacenar en base de datos
  DynamicJsonDocument doc(1024);
  doc["nombre"] = nombreUsuario;
  doc["materia"] = nombreMateria;
  doc["duracionDeseada"] = tiempoDeseado;
  doc["duracionFinal"] = tiempoTotal;
  doc["distracciones"] = totalDistracciones;
  doc["distracciones_movimiento"] = contadorMovimientos;
  doc["distracciones_sonido"] = contadorSonidos;
  doc["luz_promedio"] = promedioLuz;
  doc["temperatura_promedio"] = promedioTemperatura;
  doc["ruido_promedio"] = promedioSonido;
  doc["fecha"] = fechaHoraActual;
  
  String jsonOutput;
  serializeJson(doc, jsonOutput);
  Serial.println("DB_DATA:" + jsonOutput);
  
  // Mostrar en consola
  Serial.println("Sesión finalizada");
  Serial.print("Duración total (segundos): ");
  Serial.println(tiempoTotal / 1000);
  Serial.print("Distracciones: ");
  Serial.println(totalDistracciones);
}

// Función para leer sensores
void leerSensores() {
  // Leer temperatura con DHT11
  float temp = dht.readTemperature();
  if (!isnan(temp)) {
    temperaturaActual = temp;
    sumaTemperatura += temp;
    Serial.print("Temperatura actual (°C): ");
    Serial.println(temperaturaActual);
  } else {
    Serial.println("Error al leer el sensor DHT11");
  }

  // Lecturas de luz, sonido, movimiento
  luzActual = analogRead(SENSOR_LUZ);
  sumaLuz += luzActual;
  Serial.print("Nivel de luz: "); Serial.println(luzActual);

  sonidoActual = analogRead(SENSOR_SONIDO);
  sumaSonido += sonidoActual;
  Serial.print("Nivel de sonido: "); Serial.println(sonidoActual);

  movimientoActual = digitalRead(SENSOR_MOVIMIENTO);
  if (movimientoActual) {
    contadorMovimientos++;
    totalDistracciones++;
    Serial.println("¡Movimiento detectado!");
  }

  if (sonidoActual > 500) {
    contadorSonidos++;
    totalDistracciones++;
    Serial.println("¡Ruido fuerte detectado!");
  }

  totalLecturas++;
}


// Función para actualizar el tiempo restante
void actualizarTiempoRestante() {
  if (sesionActiva) {
    unsigned long tiempoTranscurrido = millis() - tiempoInicio;
    
    // Si se ha superado el tiempo deseado, finalizar la sesión
    if (tiempoTranscurrido >= tiempoDeseado) {
      finalizarSesion();
    } else {
      tiempoRestante = tiempoDeseado - tiempoTranscurrido;
      Serial.print("Tiempo restante (s): ");
      Serial.println(tiempoRestante / 1000);
    }
  }
}

// Función para procesar variables en plantillas HTML
String processor(const String& var) {
  if (var == "NOMBRE") return nombreUsuario;
  if (var == "MATERIA") return nombreMateria;
  if (var == "TIEMPO_DESEADO") return String(tiempoDeseado / 60000);
  if (var == "TIEMPO_RESTANTE") return String(tiempoRestante / 1000);
  if (var == "FECHA_HORA") return fechaHoraActual;
  if (var == "DISTRACCIONES") return String(totalDistracciones);
  if (var == "DISTRACCIONES_MOVIMIENTO") return String(contadorMovimientos);
  if (var == "DISTRACCIONES_SONIDO") return String(contadorSonidos);
  if (var == "LUZ_PROMEDIO") return String(promedioLuz);
  if (var == "TEMPERATURA_PROMEDIO") return String(promedioTemperatura);
  if (var == "RUIDO_PROMEDIO") return String(promedioSonido);
  return String();
}

// Configuración inicial
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Iniciando Sistema de Monitoreo de Concentración");

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(BUZZER, OUTPUT);  // Corregido - eliminado **InterruptedException**
  pinMode(SENSOR_MOVIMIENTO, INPUT);
  pinMode(SENSOR_LUZ, INPUT);
  pinMode(SENSOR_SONIDO, INPUT);

  dht.begin();  // Inicializar DHT11
  inicializarLittleFS();
  conectarWiFi();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  configurarServidor();
  server.begin();

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_ROJO, HIGH);
}

// Bucle principal
void loop() {
  if (lecturaInterval.update()) {
    if (sesionActiva) {
      leerSensores();
      actualizarTiempoRestante();
    }
  }
}