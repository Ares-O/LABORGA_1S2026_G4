#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

// --- CONFIGURACIÓN DE MÓDULOS ---
SoftwareSerial BT(13, A0); // Bluetooth: TX del modulo a Pin 13, RX del modulo a Pin A0
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// --- DEFINICIÓN DE PINES ---
const int pinServo = 9;
const int pinBotonPuerta = 2;
const int pinVentilador = 3;  // Pin PWM para el transistor
const int pinLedAzul = 4;     // Sistema Listo
const int pinLedVerde = 5;    // Éxito
const int pinLedRojo = 6;     // Error

// Pines de Ambientes (Leds de la casa)
const int pinsAmbientes[] = {7, 8, 10, 11, 12}; // Sala, Comedor, Cocina, Bano, Hab

// --- VARIABLES GLOBALES ---
Servo miPuerta;
bool puertaAbierta = false;
int posActual = 0; 

// Variables para Modo Fiesta
bool modoFiestaActivo = false;
unsigned long tiempoAnteriorFiesta = 0;
bool estadoLucesFiesta = false;

void setup() {
  Serial.begin(9600); // Para recibir el .org desde la PC
  BT.begin(9600);     // Para recibir comandos desde el celular
  
  lcd.init();
  lcd.backlight();
  
  miPuerta.attach(pinServo);
  miPuerta.write(0); 
  
  pinMode(pinBotonPuerta, INPUT_PULLUP);
  pinMode(pinVentilador, OUTPUT);
  pinMode(pinLedAzul, OUTPUT);
  pinMode(pinLedVerde, OUTPUT);
  pinMode(pinLedRojo, OUTPUT);
  
  for(int i=0; i<5; i++) pinMode(pinsAmbientes[i], OUTPUT);

  // Al arrancar, recuperamos el último estado guardado en la memoria EEPROM
  recuperarEstadoEEPROM();
  
  digitalWrite(pinLedAzul, HIGH);
  lcd.setCursor(0, 0);
  lcd.print("CASA INTELIGENTE");
  lcd.setCursor(0, 1);
  lcd.print("SISTEMA LISTO");
  delay(2000);
  lcd.clear();
}

// --- FUNCIONES DE MEMORIA EEPROM ---
void guardarEstadoEEPROM(int ambiente, int estado) {
  EEPROM.update(ambiente, estado); 
}

void recuperarEstadoEEPROM() {
  for(int i=0; i<5; i++) {
    int estado = EEPROM.read(i);
    digitalWrite(pinsAmbientes[i], estado);
  }
}

// --- FUNCIONES DE ACTUADORES ---
void moverPuerta(int destino) {
  if (posActual < destino) {
    for (int i = posActual; i <= destino; i++) {
      miPuerta.write(i);
      delay(25); 
    }
  } else {
    for (int i = posActual; i >= destino; i--) {
      miPuerta.write(i);
      delay(25); 
    }
  }
  posActual = destino;
}

void apagarTodo() {
  modoFiestaActivo = false;
  analogWrite(pinVentilador, 0);
  for(int i=0; i<5; i++) {
    digitalWrite(pinsAmbientes[i], LOW);
    guardarEstadoEEPROM(i, LOW);
  }
}

// --- PARSEO DEL ARCHIVO .ORG ---
void analizarArchivoOrg(String datos) {
  // Formato esperado: conf_ini|1,0,1,1,0|150|conf:fin
  int p1 = datos.indexOf('|');
  int p2 = datos.indexOf('|', p1 + 1);
  int p3 = datos.indexOf('|', p2 + 1);
  
  if (p1 != -1 && p2 != -1 && p3 != -1) {
    String subLuces = datos.substring(p1 + 1, p2);
    String subVent = datos.substring(p2 + 1, p3);
    
    // Configurar Ventilador
    int vel = subVent.toInt();
    analogWrite(pinVentilador, vel);

    // Configurar Luces (saltando las comas)
    int ambIdx = 0;
    for(int i = 0; i < subLuces.length(); i++) {
      char c = subLuces.charAt(i);
      if(c == '1' || c == '0') {
        int edo = (c == '1') ? HIGH : LOW;
        digitalWrite(pinsAmbientes[ambIdx], edo);
        guardarEstadoEEPROM(ambIdx, edo);
        ambIdx++;
        if(ambIdx >= 5) break;
      }
    }
    mostrarEstado("CONFIG CARGADA", true);
  } else {
    mostrarEstado("ERROR FORMATO", false);
  }
}

// --- BUCLE PRINCIPAL ---
void loop() {
  // 1. Efecto Fiesta (Sin bloqueos)
  if (modoFiestaActivo) {
    unsigned long tActual = millis();
    if (tActual - tiempoAnteriorFiesta >= 300) {
      tiempoAnteriorFiesta = tActual;
      estadoLucesFiesta = !estadoLucesFiesta;
      for(int i=0; i<5; i++) {
        digitalWrite(pinsAmbientes[i], (i % 2 == 0) ? estadoLucesFiesta : !estadoLucesFiesta);
      }
    }
  }

  // 2. Control por Botón Físico
  if (digitalRead(pinBotonPuerta) == LOW) {
    delay(50);
    puertaAbierta = !puertaAbierta;
    if (puertaAbierta) { mostrarEstado("ABRIENDO...", true); moverPuerta(90); }
    else { mostrarEstado("CERRANDO...", true); moverPuerta(0); }
    while(digitalRead(pinBotonPuerta) == LOW);
  }

  // 3. Lectura de Comandos (PC o Bluetooth)
  String entrada = "";
  if (BT.available() > 0) entrada = BT.readStringUntil('\n');
  else if (Serial.available() > 0) entrada = Serial.readStringUntil('\n');

  if (entrada != "") {
    entrada.trim();
    procesarComando(entrada);
  }
}

void procesarComando(String cmd) {
  // Prioridad 1: ¿Es un archivo de configuración?
  if (cmd.startsWith("conf_ini")) {
    if (cmd.endsWith("conf:fin")) {
      analizarArchivoOrg(cmd);
    } else {
      mostrarEstado("ERROR SINTAXIS", false);
    }
    return;
  }

  // Prioridad 2: Comandos de Escenas y Control
  if (cmd == "modo_fiesta") {
    apagarTodo();
    modoFiestaActivo = true;
    analogWrite(pinVentilador, 125);
    mostrarEstado("MODO: FIESTA", true);
  } 
  else if (cmd == "modo_relajado" || cmd == "modo_noche") {
    apagarTodo();
    mostrarEstado("MODO: RELAJADO", true);
  }
  else if (cmd == "abrir_puerta") {
    puertaAbierta = true;
    mostrarEstado("ABRIENDO...", true);
    moverPuerta(90);
  }
  else if (cmd == "cerrar_puerta") {
    puertaAbierta = false;
    mostrarEstado("CERRANDO...", true);
    moverPuerta(0);
  }
  else if (cmd == "encender_todo") {
    modoFiestaActivo = false;
    analogWrite(pinVentilador, 125);
    for(int i=0; i<5; i++) {
      digitalWrite(pinsAmbientes[i], HIGH);
      guardarEstadoEEPROM(i, HIGH);
    }
    mostrarEstado("TODO ENCENDIDO", true);
  }
  else if (cmd == "apagar_todo") {
    apagarTodo();
    mostrarEstado("TODO APAGADO", true);
  }
  else {
    mostrarEstado("CMD INVALIDO", false);
  }
}

void mostrarEstado(String msg, bool exito) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(msg);
  digitalWrite(exito ? pinLedVerde : pinLedRojo, HIGH);
  delay(500);
  digitalWrite(exito ? pinLedVerde : pinLedRojo, LOW);
}