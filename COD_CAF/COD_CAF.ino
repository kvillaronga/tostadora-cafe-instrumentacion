// Código ESP32 - recibe datos del formulario HTML en /recibir
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include "max6675.h"
#include "ThingSpeak.h"   // --- NUEVO ---

WebServer server(80);
WiFiClient client;        // --- NUEVO ---

// --- CONFIGURACIÓN WIFI ---
const char* ssid = "USTA_Estudiantes";
const char* password = "#soytomasino";

// ---- CONFIGURACIÓN THINGSPEAK ----
unsigned long myChannelNumber = 3149308;         // --- TU CANAL ---
const char* myWriteAPIKey = "CP9KQ53X44E3FE4C";  // --- TU API KEY ---

// ---- MAX6675 ----
const int ktcSO = 19; 
const int ktcCS = 5;
const int ktcSCK = 18;

// PINES DEL MOTOR
int ENA =  25; // PWM
int IN1 =  26;
int IN2 =  27;

MAX6675 thermocouple(ktcSCK, ktcCS, ktcSO);

// Variables HTML
String masa_str = "";
String velocidad_str = "";
String tostion_str = "";

float masa_val = 0.0;
int velocidad_nivel = 0;
int tipo_tostion = 0;

// Variables proceso
bool tostion_activa = false;
unsigned long inicio_tostion = 0;
unsigned long duracion_tostion = 0;
unsigned long ultimo_reporte = 0;

// ---------- ECUACIÓN DE CALIBRACIÓN ----------
double calibrar(double Traw) {
  if (isnan(Traw)) return -1;
  double Tcal = 1.003*Traw + 0.45 + 0.00011*(Traw*Traw);
  if (Tcal < 0) Tcal = 0;
  if (Tcal > 600) Tcal = 600;
  return Tcal;
}

// ---------- MODELO DE TIEMPO ----------
unsigned long calcularTiempoTueste(float masa_g, int nivel_flama, int tipo_tost) {
  double masa_kg = masa_g / 1000.0;
  if (masa_kg < 0.05) masa_kg = 0.05;

  double base_per_kg = 600.0;
  double factor_flama = (nivel_flama == 1) ? 1.12 : (nivel_flama == 3 ? 0.82 : 1.0);
  double factor_tostion = (tipo_tost == 1) ? 0.85 : (tipo_tost == 3 ? 1.28 : 1.0);

  double tiempo = base_per_kg * masa_kg * factor_flama * factor_tostion;
  tiempo *= (1.0 + 0.04 * log10(masa_kg + 0.1));

  if (tiempo < 30) tiempo = 30;
  if (tiempo > 7200) tiempo = 7200;
  return (unsigned long) tiempo;
}

// --------- CONTROL DEL MOTOR ---------
void encenderMotor() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 200);   // ahora ENA se usa como salida digital
  Serial.println("Motor: ENCENDIDO");
}

void apagarMotor() {
  analogWrite(ENA, 200);    // apaga el motor
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  Serial.println("Motor: APAGADO");
}

// --------- HANDLERS WEB ---------
void handleRoot() {
  server.send(200, "text/plain", "ESP32 listo para recibir datos en /recibir");
}

void handleRecibir() {
  masa_str = server.arg("masa");
  velocidad_str = server.arg("velocidad");
  tostion_str = server.arg("tostion");

  masa_val = masa_str.toFloat();
  velocidad_nivel = (velocidad_str == "bajo") ? 1 : (velocidad_str == "alto" ? 3 : 2);
  tipo_tostion = (tostion_str == "claro") ? 1 : (tostion_str == "oscuro" ? 3 : 2);

  duracion_tostion = calcularTiempoTueste(masa_val, velocidad_nivel, tipo_tostion) * 1000UL;
  inicio_tostion = millis();
  ultimo_reporte = millis();
  tostion_activa = true;

  encenderMotor();

  Serial.println("=== DATOS RECIBIDOS ===");
  Serial.print("Masa: "); Serial.println(masa_val);
  Serial.print("Velocidad: "); Serial.println(velocidad_nivel);
  Serial.print("Tostión: "); Serial.println(tipo_tostion);
  Serial.print("Tiempo (s): "); Serial.println(duracion_tostion / 1000);
  Serial.println("========================");

  String html = "<html><body><h3>Datos recibidos. Tostión iniciada.</h3>";
  html += "<p>Tiempo estimado: " + String(duracion_tostion/1000) + " s</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// --------- SETUP ---------
void setup() {
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);   // necesario para ThingSpeak
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nConectado!");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/recibir", handleRecibir);
  server.begin();

  // Inicializar ThingSpeak
  ThingSpeak.begin(client);
}

// --------- LOOP ---------
void loop() {
  server.handleClient();

  if (tostion_activa) {
    unsigned long ahora = millis();
    unsigned long transcurrido = ahora - inicio_tostion;

    if (ahora - ultimo_reporte >= 30000) {
      ultimo_reporte = ahora;

      Serial.println("----- REPORTE -----");
      Serial.print("Total: "); Serial.println(duracion_tostion / 1000);
      Serial.print("Transcurrido: "); Serial.println(transcurrido / 1000);
      Serial.print("Restante: "); Serial.println((duracion_tostion - transcurrido) / 1000);

      double Traw = thermocouple.readCelsius();
      double Tcal = calibrar(Traw);
      Serial.print("Temp cruda: "); Serial.println(Traw);
      Serial.print("Temp calibrada: "); Serial.println(Tcal);
      Serial.println("-------------------");

      // --------- ENVÍO A THINGSPEAK ---------
      ThingSpeak.setField(1, (float)Tcal);   // campo 1 = temperatura calibrada
      int httpCode = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

      if (httpCode == 200) {
        Serial.println("Dato enviado correctamente a ThingSpeak.");
      } else {
        Serial.print("Error al enviar datos. Código HTTP: ");
        Serial.println(httpCode);
      }
    }

    if (transcurrido >= duracion_tostion) {
      tostion_activa = false;
      apagarMotor();
      Serial.println("*** TOSTIÓN FINALIZADA ***");
    }
  }
}
