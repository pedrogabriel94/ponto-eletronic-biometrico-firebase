#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <time.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <SoftwareSerial.h>
#include <Adafruit_Fingerprint.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define WIFI_SSID "MOB-THALIA"
#define WIFI_PASSWORD "enscn123"

#define API_KEY "-"
#define DATABASE_URL "-"
#define USER_EMAIL "-"
#define USER_PASSWORD "-"

SoftwareSerial mySerial(13, 12);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson json;

void setup() {
  // Configura o NTP (UTC-3 para Brasil)
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  Wire.begin(4, 5);
  finger.begin(57600);

  lcd.init();           // Inicializa o LCD
  lcd.backlight();      // Liga o backlight
  delay(100);           // Pequeno delay após inicialização

  conectWifi();
  conectFirebase();

  if (finger.verifyPassword()) {
    Serial.println("Sensor biométrico encontrado!");
    Serial.print("Total de digitais cadastradas: ");
    Serial.println(finger.getTemplateCount());
  } else {
    Serial.println("Sensor biométrico não encontrado.");
  }

  //clearAllFingerprints();
}

void conectWifi(){
  Serial.begin(9600);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
}

void conectFirebase(){
  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;

  Firebase.reconnectNetwork(true);

  fbdo.setBSSLBufferSize(4096 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);

  // Limit the size of response payload to be collected in FirebaseData
  fbdo.setResponseSize(2048);
  Firebase.begin(&config, &auth);
  
  // The WiFi credentials are required for Pico W
  // due to it does not have reconnect feature.
  #if defined(ARDUINO_RASPBERRY_PI_PICO_W)
    config.wifi.clearAP();
    config.wifi.addAP(WIFI_SSID, WIFI_PASSWORD);
  #endif

  Firebase.setDoubleDigits(5);
  config.timeout.serverResponse = 10 * 1000;
}

String obterDataFormatada(){

  // Configura o NTP (UTC-3 para Brasil)
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char data[11]; // "yyyy-mm-dd" + '\0'
    strftime(data, sizeof(data), "%Y-%m-%d", &timeinfo);
    return String(data);
  } else {
    return "0000-00-00"; // fallback em caso de erro
  }
}

String obterDataFormatadaLcd(){
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char data[17]; // "yyyy-mm-dd" + '\0'
    strftime(data, sizeof(data), "%d/%m/%Y %H:%M", &timeinfo);
    return String(data);
  } else {
    return "0000-00-00"; // fallback em caso de erro
  }
}

unsigned long getTimestamp() {
  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  time_t now;
  time(&now);

  if (now > 100000) { // verifica se a hora foi sincronizada
    return now;
  } else {
    return 0; // retorno padrão em caso de erro
  }
}

void registrarFrequencia(int id){
  String dataHoje = obterDataFormatada();
  unsigned long timestamp = getTimestamp();

  json.set("id", id);
  json.set("timestamp", timestamp);

  // Enviar dados para o Firebase
  if (Firebase.set(fbdo, "/registros/"+dataHoje+"/"+id, json) && Firebase.set(fbdo, "/alunos/"+String(id)+"/treinos/"+dataHoje, json)) {
    Serial.println("Dado enviado com sucesso!");
  } else {
    Serial.println("Erro ao enviar: " + fbdo.errorReason());
  }
}

String nomeAluno(int id){
  Firebase.getString(fbdo, "/alunos/"+String(id)+"/nome");
  return fbdo.stringData();
}

void getFingerprintID() {
  esperarDedoFixo(2000);

  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    delay(300);
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    delay(300);
  }

  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    String nome = nomeAluno(finger.fingerID);
    lcd.clear();
    lcd.setCursor(1, 0);
    lcd.print("Seja Bem-Vindo!");
    lcd.setCursor(0, 1);
    lcd.print(nome);
    registrarFrequencia(finger.fingerID);
  } else {
    Serial.println("Digital não encontrada. Cadastrando...");
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("Cadastrando...");
    if (finger.getTemplateCount() >= 127) {
      Serial.println("Limite de digitais atingido.");
      lcd.clear();
      lcd.print("Memoria cheia");
      delay(2000);
    }
    enrollFingerprint();
  }
}

int enrollFingerprint() {
  int id = 1;
  while (finger.loadModel(id) == FINGERPRINT_OK && id < 127) {
    id++;
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Cadastro. ID: ");
  lcd.setCursor(0, 1);
  lcd.print(id);

  while (true) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Aproxime o dedo");
    if (finger.getImage() != FINGERPRINT_OK) continue;
    if (finger.image2Tz(1) != FINGERPRINT_OK) continue;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Remova o dedo...");
    delay(2000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Aproxime o dedo");
    lcd.setCursor(0, 1);
    lcd.print("novamente");

    while (finger.getImage() != FINGERPRINT_OK);
    if (finger.image2Tz(2) != FINGERPRINT_OK) continue;

    if (finger.createModel() != FINGERPRINT_OK) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Falha");
      return -1;
    }

    if (finger.storeModel(id) == FINGERPRINT_OK) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Sucesso!");
      return id;
    } else {
      Serial.println("Falha ao salvar modelo");
      return -1;
    }
  }
}

bool esperarDedoFixo(int tempoMs) {
  unsigned long inicio = millis();
  while (millis() - inicio < tempoMs) {
    if (finger.getImage() != FINGERPRINT_OK) {
      inicio = millis(); // reinicia se tirar o dedo
    }
    delay(50);
  }
  return true;
}

void clearAllFingerprints() {
  Serial.println("Limpando todas as digitais...");
  int p = finger.emptyDatabase();  // Deleta todos os modelos de digitais armazenados
  
  if (p == FINGERPRINT_OK) {
    Serial.println("Todas as digitais foram limpas com sucesso!");
  } else {
    Serial.print("Erro ao limpar digitais. Código de erro: ");
    Serial.println(p);
  }
}

void loop() {

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(obterDataFormatadaLcd());
  lcd.setCursor(0, 1);
  lcd.print("Aproxime o dedo!");

  getFingerprintID();
}


