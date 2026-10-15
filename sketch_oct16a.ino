#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h> // DODATO ZA OTA
#include <time.h>

const char* ssid     = "A1_SRB_51B2BA-2.4GHz";
const char* password = "Intel321.";

IPAddress staticnaIP(192, 168, 100, 199);
IPAddress gateway   (192, 168, 100,   1);
IPAddress subnet    (255, 255, 255, 0);
IPAddress dns1      (  8,   8,   8, 8);

// Unesi link svog Firebase-a (npr. "https://tvoj-projekat-default-rtdb.europe-west1.firebasedatabase.app")
const char* FB = "https://brod1pozarevacsrbija-default-rtdb.europe-west1.firebasedatabase.app/";

WiFiClientSecure wifiClient;

// ─── VERZIJA KODA (Menjaš ovo kada izbaciš novu verziju na GitHub) ───────────
const int TRENUTNA_VERZIJA = 1; 

// ─── HC-SR04 pinovi ──────────────────────────────────────────────────────────
#define TRIG_PIN 5
#define ECHO_PIN 18

int          brojacKorpi        = 0;
bool         korpaJeTu          = false;
int          trenutnaUdaljenost = 0;
unsigned long poslednjeBrojanje  = 0;
unsigned long poslednjeDist      = 0;
unsigned long poslednjaProvera   = 0;
unsigned long poslednjiOtaCheck  = 0; // Za tajmer OTA provere

const int          GRANICA_DETEKCIJE   = 50;   // 250cm = 2.5m
const int          HISTEREZIS          = 10;    // 50cm
const int          BROJ_UZORAKA        = 5;
const unsigned long PAUZA_IZMEDJU_KORPI = 10000;
const unsigned long INTERVAL_DIST_MS    = 2000;
const unsigned long INTERVAL_FLAG_MS    = 3000;
const unsigned long INTERVAL_OTA_MS     = 3600000; // Proveravaj update na svakih 1 sat

void setupNTP() {
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
  Serial.print("NTP sinhronizacija");
  struct tm t;
  int pokusaji = 0;
  while (!getLocalTime(&t) && pokusaji < 20) {
    delay(500); Serial.print("."); pokusaji++;
  }
  if (pokusaji < 20) Serial.println("\nNTP OK");
  else               Serial.println("\nNTP nije uspeo — koristim uptime");
}

String vremeString() {
  struct tm t;
  if (getLocalTime(&t)) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d:%02d",
             t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
             t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
  }
  unsigned long s = millis() / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), "--.--.---- %02lu:%02lu:%02lu",
           (s/3600)%24, (s/60)%60, s%60);
  return String(buf);
}

bool fbPatch(const String& path, const String& json) {
  HTTPClient http;
  http.begin(wifiClient, String(FB) + path + ".json");
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest("PATCH", json);
  http.end();
  return code == 200;
}

String fbGet(const String& path) {
  HTTPClient http;
  http.begin(wifiClient, String(FB) + path + ".json");
  String body = "";
  if (http.GET() == 200) body = http.getString();
  http.end();
  return body;
}

void fbDetekcija() {
  String vreme = vremeString();
  String root = "{\"object_count\":" + String(brojacKorpi) +
                ",\"sensor_near\":true" +
                ",\"last_detection\":\"" + vreme + "\"}";
  fbPatch("/", root);
  HTTPClient http;
  String logJson = "{\"time\":\"" + vreme + "\",\"count\":" + String(brojacKorpi) + "}";
  http.begin(wifiClient, String(FB) + "/detection_log.json");
  http.addHeader("Content-Type", "application/json");
  http.POST(logJson);
  http.end();
  Serial.println("Firebase: detekcija #" + String(brojacKorpi) + " @ " + vreme);
}

void fbUpdateDist() {
  String json = "{\"distance\":" + String(trenutnaUdaljenost) +
                ",\"sensor_near\":" + (korpaJeTu ? "true" : "false") + "}";
  fbPatch("/", json);
}

void fbProveriFlag() {
  String val = fbGet("/reset_flag");
  val.trim();
  if (val == "true") {
    brojacKorpi       = 0;
    korpaJeTu         = false;
    poslednjeBrojanje = 0;
    fbPatch("/", "{\"reset_flag\":false,\"object_count\":0,\"sensor_near\":false}");
    Serial.println("Reset flag — brojac resetovan");
  }
}

// ─── PROVERA I OTA AŽURIRANJE PREKO FIREBASE-a / GITHUB-a ─────────────────────
void proveriFirmwareUpdate() {
  Serial.println("Proveravam da li postoji nova verzija koda na Firebase-u...");
  
  // Čitamo verziju sa Firebase-a (npr. čvor /system/firmware_version)
  String vStr = fbGet("/system/firmware_version");
  vStr.trim();
  vStr.replace("\"", ""); // Uklanjamo navodnike koje Firebase vrati
  
  int firebaseVerzija = vStr.toInt();
  
  if (firebaseVerzija > TRENUTNA_VERZIJA) {
    Serial.println("Pronađena nova verzija! Preuzimam link za .bin fajl...");
    
    // Čitamo direktan link ka GitHub .bin fajlu sa Firebase-a (/system/firmware_url)
    String urlStr = fbGet("/system/firmware_url");
    urlStr.trim();
    urlStr.replace("\"", "");
    
    if (urlStr.length() > 10) {
      Serial.println("Preuzimam update sa linka: " + urlStr);
      
      // Pokrećemo OTA preuzimanje i flešovanje preko sigurnog wifiClient-a
      t_httpUpdate_return ret = httpUpdate.update(wifiClient, urlStr);

      switch (ret) {
        case HTTP_UPDATE_FAILED:
          Serial.printf("Greška pri ažuriranju (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
          break;
        case HTTP_UPDATE_NO_UPDATES:
          Serial.println("Nema ažuriranja.");
          break;
        case HTTP_UPDATE_OK:
          Serial.println("Ažuriranje uspešno! ESP32 se restartuje sa novim kodom...");
          break;
      }
    }
  } else {
    Serial.println("Kod je up-to-date (Verzija: " + String(TRENUTNA_VERZIJA) + ")");
  }
}

// ─── HC-SR04 merenje ─────────────────────────────────────────────────────────

int izmeriCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long trajanje = pulseIn(ECHO_PIN, HIGH, 30000); // timeout 30ms = ~5m max
  if (trajanje == 0) return 0; // nema odjeka = van opsega

  int cm = trajanje / 58;
  return cm;
}

int procitajPrecizno() {
  long suma = 0;
  int validnih = 0;
  for (int i = 0; i < BROJ_UZORAKA; i++) {
    int val = izmeriCm();
    if (val > 2) { // ignoriši šum ispod 2cm
      suma += val;
      validnih++;
    }
    delay(60); // HC-SR04 treba min 60ms između merenja
  }
  if (validnih == 0) return 0;
  int prosek = suma / validnih;
  return prosek;
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.begin(115200);
  wifiClient.setInsecure();

  if (!WiFi.config(staticnaIP, gateway, subnet, dns1))
    Serial.println("Greška pri konfiguraciji statičke IP!");

  WiFi.begin(ssid, password);
  Serial.print("Spajam se na WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  setupNTP();

  // Proveri update odmah pri pokretanju uređaja
  proveriFirmwareUpdate();

  Serial.println("Sve OK — HC-SR04 Firebase + OTA mod");
}

void loop() {
  trenutnaUdaljenost = procitajPrecizno();

  unsigned long now          = millis();
  bool         vremeIsteklo = (now - poslednjeBrojanje >= PAUZA_IZMEDJU_KORPI);

  // 0 = van opsega = slobodno
  if (trenutnaUdaljenost > 2 && trenutnaUdaljenost <= GRANICA_DETEKCIJE) {
    if (!korpaJeTu && vremeIsteklo) {
      delay(100);
      int potvrda = procitajPrecizno();
      if (potvrda > 2 && potvrda <= GRANICA_DETEKCIJE) {
        brojacKorpi++;
        korpaJeTu         = true;
        poslednjeBrojanje = millis();
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println(">>> KORPA #" + String(brojacKorpi) + " <<<");
        fbDetekcija();
      }
    }
  } else if (trenutnaUdaljenost == 0 || trenutnaUdaljenost > (GRANICA_DETEKCIJE + HISTEREZIS)) {
    if (korpaJeTu) {
      korpaJeTu = false;
      digitalWrite(LED_BUILTIN, LOW);
    }
  }

  if (now - poslednjeDist >= INTERVAL_DIST_MS) {
    poslednjeDist = now;
    fbUpdateDist();
  }

  if (now - poslednjaProvera >= INTERVAL_FLAG_MS) {
    poslednjaProvera = now;
    fbProveriFlag();
  }

  // Redovna provera da li ima novog koda na svak sat vremena
  if (now - poslednjiOtaCheck >= INTERVAL_OTA_MS) {
    poslednjiOtaCheck = now;
    proveriFirmwareUpdate();
  }
}