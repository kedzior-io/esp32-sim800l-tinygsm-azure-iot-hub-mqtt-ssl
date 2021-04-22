#define DUMP_AT_COMMANDS

#define TINY_GSM_SSL_CLIENT_AUTHENTICATION
#define SIM800L_IP5306_VERSION_20190610
#define TINY_GSM_MODEM_SIM800

#define SerialMon Serial
#define SerialAT Serial1

// Define the serial console for debug prints, if needed
#define TINY_GSM_DEBUG SerialMon

#include "keys.h"
#include "lilygo.h"
#include <TinyGsmClient.h>
#include <PubSubClient.h>

// certificate
#include "cert-azure-iot-baltimore.h"
#define CERT_FILE "C:\\projects\\github\\esp32-sim800l-tinygsm-azure-iot-hub-mqtt-ssl\\azure-iot-baltimore.crt"

#ifdef DUMP_AT_COMMANDS
  #include <StreamDebugger.h>
  StreamDebugger debugger(SerialAT, SerialMon);
  TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

TinyGsmClientSecure client(modem);
PubSubClient mqtt(client);

uint32_t lastReconnectAttempt = 0;

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  SerialMon.print("Message arrived [");
  SerialMon.print(topic);
  SerialMon.print("]: ");
  SerialMon.write(payload, len);
  SerialMon.println();
}

boolean mqttConnect() {
  SerialMon.print("Connecting to ");
  SerialMon.print(MQTT_BROKER);

  boolean status = mqtt.connect(MQTT_DEVICE, MQTT_USERNAME, MQTT_PASS);

  if (status == false) {
    SerialMon.println("Failed to reconnect to the broker.");
    SerialMon.print("Status: ");
    SerialMon.println(mqtt.state());
    return false;
  }

  SerialMon.println(" success");
}

void SetupCert()
{

  modem.sendAT(GF("+FSCREATE=" CERT_FILE));
  if (modem.waitResponse() != 1) return;

  const int cert_size = sizeof(cert);

  modem.sendAT(GF("+FSWRITE=" CERT_FILE ",0,"), cert_size, GF(",10"));
  
  if (modem.waitResponse(GF(">")) != 1) {
    return;
  }

  for (int i = 0; i < cert_size; i++) {
    char c = pgm_read_byte(&cert[i]);
    modem.stream.write(c);
  }

  modem.stream.write(GSM_NL);
  modem.stream.flush();

  if (modem.waitResponse(2000) != 1) return;

  modem.sendAT(GF("+SSLSETCERT=\"" CERT_FILE "\""));
  if (modem.waitResponse() != 1) return;
  if (modem.waitResponse(5000L, GF(GSM_NL "+SSLSETCERT:")) != 1) return;
  const int retCode = modem.stream.readStringUntil('\n').toInt();


  SerialMon.println();
  SerialMon.println();
  SerialMon.println(F("****************************"));
  SerialMon.print(F("Setting Certificate: "));
  SerialMon.println((0 == retCode) ? "OK" : "FAILED");
  SerialMon.println(F("****************************"));
}


void setup() {
  // Set console baud rate
  SerialMon.begin(115200);
  delay(10);

  // Set modem pins

  // Keep reset high
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, HIGH);
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);

  // Turn on the Modem power first
  digitalWrite(MODEM_POWER_ON, HIGH);

  // Pull down PWRKEY for more than 1 second according to manual requirements
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(100);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);

  // Initialize the indicator as an output
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LED_OFF);

  SerialMon.println("Wait...");

  // Set GSM module baud rate
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(6000);

  // Restart takes quite some time
  // To skip it, call init() instead of restart()
  SerialMon.println("Initializing modem...");
  //modem.restart();
  modem.init();

  SetupCert();

  String modemInfo = modem.getModemInfo();
  SerialMon.print("Modem Info: ");
  SerialMon.println(modemInfo);

  // for Azure TLS 1.2 and the SIM800L
  modem.sendAT("+SSLOPT=1,1");
  int rsp = modem.waitResponse();
  if (rsp != 1)
  {
    Serial.printf("modem +SSLOPT=1,1 failed");
  }   

  // Unlock your SIM card with a PIN if needed
  if (SIM_PIN && modem.getSimStatus() != 3 ) {
    modem.simUnlock(SIM_PIN);
  }


  SerialMon.print("Waiting for network...");
  if (!modem.waitForNetwork()) {
    SerialMon.println(" fail");
    delay(10000);
    return;
  }
  SerialMon.println(" success");

  if (modem.isNetworkConnected()) {
    SerialMon.println("Network connected");
  }

  // GPRS connection parameters are usually set after network registration
  SerialMon.print(F("Connecting to "));
  SerialMon.print(APN);
  if (!modem.gprsConnect(APN, APN_USER, APN_PASS)) {
    SerialMon.println(" fail");
    delay(10000);
    return;
  }
  SerialMon.println(" success");

  if (modem.isGprsConnected()) {
    SerialMon.println("GPRS connected");
  }

  // MQTT Broker setup
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

void loop() {

  if (!mqtt.connected()) {
    SerialMon.println("=== MQTT NOT CONNECTED ===");
    // Reconnect every 10 seconds
    uint32_t t = millis();
    if (t - lastReconnectAttempt > 10000L) {
      lastReconnectAttempt = t;
      if (mqttConnect()) {
        lastReconnectAttempt = 0;
      }
    }
    delay(100);
    return;
  }

  mqtt.loop();
}