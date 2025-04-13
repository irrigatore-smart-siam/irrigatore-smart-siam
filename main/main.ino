#define BLYNK_TEMPLATE_ID "TMPL4FQ929HOm"
#define BLYNK_TEMPLATE_NAME "MyLolin"
#define BLYNK_AUTH_TOKEN "VprRzFgS-tXN2kLCdVn8oI8cR-DCeJss"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include "arduino_secrets.h"  
#include "thingProperties.h"

// Definizione pin
const uint8_t MOISTURESENSORPIN = 7;  // Sensore di umidità
const uint8_t RELAYPIN = 9;           // Pompa
const uint8_t TRIGPIN = 3;            // Trigger del sensore a ultrasuoni
const uint8_t ECHOPIN = 5;            // Echo del sensore a ultrasuoni
const uint8_t RED_PIN = 11;           // LED rosso
const uint8_t GREEN_PIN = 12;         // LED verde
const uint8_t BLUE_PIN = 16;          // LED blu

// Costanti
const uint16_t SOGLIA_UM = 7000;     // Soglia umidità
const float MAX_TANK_DEPTH = 10.0;   // Profondità massima del serbatoio in cm
const float SOUND_SPEED = 0.0343;    // Velocità del suono cm/microsecondo

// Variabili di stato
unsigned long pumpTimer = 0;
bool pumpOn = false;
bool isManualMode = false;          // Flag per il controllo manuale
unsigned long lastActivationTime = 0;
float lastWaterPercentage = 0;
unsigned long cloudSwitchResetTimer = 0;
const unsigned long CLOUD_SWITCH_RESET_DELAY = 400; // 0.4 per reset CloudSwitch

// Timer per il lampeggiamento del LED blu
unsigned long blueBlinkTimer = 0;
bool blueBlinkState = false;
bool isBlueBlinking = false;
unsigned long blueBlinkStartTime = 0;
bool needsLedUpdate = false;

BlynkTimer timer;

// Funzioni di utilità
float measureDistance() {
  digitalWrite(TRIGPIN, LOW);
  delayMicroseconds(2);
  
  // Genera impulso trigger
  digitalWrite(TRIGPIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGPIN, LOW);
  
  // Misura durata dell'eco
  long duration = pulseIn(ECHOPIN, HIGH, 38000); // Timeout 38ms
  
  // Calcola distanza se duration è valido
  if (duration > 0) {
    float distance = duration * SOUND_SPEED / 2; // Conversione in cm
    return constrain(MAX_TANK_DEPTH - distance, 0, MAX_TANK_DEPTH);
  } else {
    Serial.println("ERRORE: Nessun impulso rilevato dal sensore ultrasuoni!");
    return -1; // Errore
  }
}

float getWaterPercentage(float distance) {
  distance = constrain(distance, 0, MAX_TANK_DEPTH); // Limita tra 0 e 10 cm
  return (distance / MAX_TANK_DEPTH) * 100.0;
}

void startBlueBlinking() {
  isBlueBlinking = true;
  blueBlinkStartTime = millis();
  blueBlinkTimer = millis();
  blueBlinkState = true;
  
  // Spegni gli altri LED durante il lampeggiamento del blu
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, HIGH);   // LED blu ON inizia acceso
}

void updateLedStatus(float waterPercentage) {
  // Funzione centralizzata per aggiornare lo stato dei LED
  if (isBlueBlinking) {
    // Durante il lampeggiamento blu, gli altri LED rimangono spenti
    return;
  }
  
  // Assicurati che il LED blu sia spento
  digitalWrite(BLUE_PIN, LOW);
  
  // Gestisci i LED rosso e verde in base al livello dell'acqua
  if (waterPercentage > 50) { // Serbatoio più che mezzo pieno
    digitalWrite(GREEN_PIN, HIGH);  // LED verde ON
    digitalWrite(RED_PIN, LOW);    // LED rosso OFF
    Blynk.virtualWrite(V3, 0);    // Rosso spento
    Blynk.virtualWrite(V4, 255);  // Verde acceso
  } else { // Serbatoio sotto il 50%
    digitalWrite(RED_PIN, HIGH);    // LED rosso ON
    digitalWrite(GREEN_PIN, LOW);  // LED verde OFF
    Blynk.virtualWrite(V3, 255);  // Rosso acceso
    Blynk.virtualWrite(V4, 0);    // Verde spento
    Blynk.logEvent("alert_acqua_scarsa");
  }
}

void updateBlueBlink() {
  if (!isBlueBlinking) return;
  
  // Verifica se il tempo di lampeggiamento di 5 secondi è terminato
  if (millis() - blueBlinkStartTime >= 5000) {
    isBlueBlinking = false;
    digitalWrite(BLUE_PIN, LOW); // Spegni il LED blu
    needsLedUpdate = true;  // Imposta il flag per aggiornare i LED al prossimo ciclo
    return;
  }
  
  // Inverti lo stato del LED blu ogni 250ms
  if (millis() - blueBlinkTimer >= 250) {
    blueBlinkTimer = millis();
    blueBlinkState = !blueBlinkState;
    if (blueBlinkState) {
      digitalWrite(BLUE_PIN, HIGH); // Blu acceso
    } else {
      digitalWrite(BLUE_PIN, LOW); // Blu spento
    }
  }
}

void checkIfLedUpdateNeeded() {
  // Nuova funzione per controllare se i LED necessitano di un aggiornamento
  if (needsLedUpdate && !isBlueBlinking) {
    needsLedUpdate = false;
    
    float distance = measureDistance();
    if (distance >= 0 && distance <= MAX_TANK_DEPTH) {
      float waterPercentage = getWaterPercentage(distance);
      updateLedStatus(waterPercentage);
    } else {
      // In caso di errore del sensore, usa l'ultimo valore valido
      updateLedStatus(lastWaterPercentage);
    }
  }
}

void checkMoisture() {
  if (isManualMode) return;  // Non controllare l'umidità in modalità manuale
  
  int moistureValue = analogRead(MOISTURESENSORPIN);
  unsigned long currentTime = millis();
  
  // Attiva la pompa automaticamente se necessario, ogni 24h
  if (moistureValue > SOGLIA_UM && !pumpOn && (currentTime - lastActivationTime >= 86400000)) {
    Serial.println("->ATTIVAZIONE POMPA (sensore)");
    digitalWrite(RELAYPIN, LOW);
    pumpOn = true;
    pumpTimer = currentTime;
    lastActivationTime = currentTime;
    
    // Attiva il lampeggiamento del LED blu per 5 secondi
    startBlueBlinking();
  }
}

void checkPumpTimer() {
  if (pumpOn && (millis() - pumpTimer >= 500) && !isManualMode) {
    Serial.println("->SPEGNIMENTO POMPA (timer)");
    digitalWrite(RELAYPIN, HIGH);
    pumpOn = false;
  }
  
  // Se la pompa è stata attivata manualmente da Alexa, controlla se è ora di disattivarla
  if (isManualMode && livello == 0 && pumpOn) {
    Serial.println("->SPEGNIMENTO POMPA (manuale)");
    digitalWrite(RELAYPIN, HIGH);
    pumpOn = false;
    isManualMode = false;
  }
}

void checkCloudSwitchReset() {
  if (livello == 1 && millis() - cloudSwitchResetTimer > CLOUD_SWITCH_RESET_DELAY) {
    livello = 0;
    Serial.println("CloudSwitch resettato automaticamente");
    ArduinoCloud.update();
  }
}

void checkWaterLevel() {
  // Se il blu sta lampeggiando, non aggiornare la percentuale dell'acqua
  // ma memorizza che serve un aggiornamento dei LED
  if (isBlueBlinking) {
    needsLedUpdate = true;
    return;
  }
  
  float distance = measureDistance();
  
  if (distance >= 0 && distance <= MAX_TANK_DEPTH) {
    float waterPercentage = getWaterPercentage(distance);
    lastWaterPercentage = waterPercentage;
    
    Serial.print("Distanza: ");
    Serial.print(distance);
    Serial.print(" cm → Livello: ");
    Serial.print(waterPercentage);
    Serial.println("%");
    
    Blynk.virtualWrite(V1, waterPercentage);
      
    // Aggiorna lo stato dei LED usando la funzione centralizzata
    updateLedStatus(waterPercentage);
  }
}

void sendMoistureToBlynk() {
  int moistureValue = analogRead(MOISTURESENSORPIN);
  moistureValue = constrain(moistureValue, 0, 8191); // Limita tra 0 e 8191 (secco = 0 umidità)
  float moisturePercentage = 100 - ((float)moistureValue / 8191.0f * 100.0f);

  Serial.print("Umidità: ");
  Serial.print(moistureValue);
  Serial.print(" → Percentuale: ");
  Serial.print(moisturePercentage);
  Serial.println("%");

  Blynk.virtualWrite(V2, moisturePercentage);
}

// Funzione che viene chiamata quando il valore di livello viene modificato nell'app
void onLivelloChange() {
  Serial.print("Livello cambiato: ");
  Serial.println(livello);
  
  if (livello == 1) {
    // Attiva la pompa manualmente
    Serial.println("->ATTIVAZIONE POMPA (manuale da Alexa)");
    digitalWrite(RELAYPIN, LOW);
    pumpOn = true;
    pumpTimer = millis();
    isManualMode = true;  // Imposta la modalità manuale
    cloudSwitchResetTimer = millis();
    
    // Attiva il lampeggiamento del LED blu
    startBlueBlinking();
  } else {
    // Disattiva la pompa se è stata attivata manualmente
    if (isManualMode && pumpOn) {
      Serial.println("->SPEGNIMENTO POMPA (manuale da Alexa)");
      digitalWrite(RELAYPIN, HIGH);
      pumpOn = false;
      isManualMode = false;
    }
  }
}

void setup() {
  // Configurazione dei pin
  pinMode(RELAYPIN, OUTPUT);
  pinMode(TRIGPIN, OUTPUT);
  pinMode(ECHOPIN, INPUT_PULLUP);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(MOISTURESENSORPIN, INPUT);

  // Inizializzazione stato pin
  digitalWrite(RELAYPIN, HIGH);
  digitalWrite(RED_PIN, LOW);    // LED rosso OFF
  digitalWrite(GREEN_PIN, LOW);  // LED verde OFF
  digitalWrite(BLUE_PIN, LOW);   // LED blu OFF

  // Inizializza comunicazione seriale
  Serial.begin(115200);
  Serial.println("Avvio del sistema di irrigazione unificato");

  // Configurazione Arduino IoT Cloud
  initProperties();
  livello = 0;  // Inizializza lo stato del CloudSwitch
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  
  // Configurazione WiFi e Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Controllo immediato dell'umidità
  int moistureValue = analogRead(MOISTURESENSORPIN);
  Serial.print("Umidità iniziale: ");
  Serial.println(moistureValue);

  if (moistureValue > SOGLIA_UM) {
    Serial.println("-> ATTIVAZIONE POMPA (Startup)");
    digitalWrite(RELAYPIN, LOW);
    pumpOn = true;
    pumpTimer = millis();
    lastActivationTime = millis();
    
    // Attiva il lampeggiamento del LED blu all'avvio se necessario
    startBlueBlinking();
  }

  // Configurazione timer
  timer.setInterval(100L, checkPumpTimer);
  timer.setInterval(100L, checkCloudSwitchReset);
  timer.setInterval(5000L, checkWaterLevel);
  timer.setInterval(10000L, sendMoistureToBlynk);
  timer.setInterval(50L, updateBlueBlink);    
  timer.setInterval(100L, checkIfLedUpdateNeeded);
}

void loop() {
  ArduinoCloud.update();  // Aggiorna lo stato di Arduino IoT Cloud
  Blynk.run();            // Gestisci la comunicazione Blynk
  timer.run();            // Esegui i timer
  
  checkMoisture();

  delay(10);
}