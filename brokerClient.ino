#include <WiFi.h>
#include <PubSubClient.h>

#define array_len(arr) sizeof(arr) / sizeof(arr[0])

const char* ssid = "----";
const char* password = "----";

const char* mqttServer = "----";
const int mqttPort = ----;
const char* mqttUser = "";
const char* mqttPassword = "";
const char* userID = "----";

WiFiClient espClient;
PubSubClient client(espClient);

char* subscribe_topics[] = { "PORT", "PIN", "SRF05" };

enum class Component { PORT,
                       PIN,
                       SRF05 };

std::vector<String> srf05_pins;
std::vector<uint8_t> subsribed_pins;

struct TopicInfo {
  String part;
  String id;
};

void brokerReconnect();
void wifiReconnect();

void callback(char* topic, byte* payload, unsigned int length);

struct TopicInfo parseTopic(const char* topic);
Component parseComponent(String str);

int srf05_distance(uint8_t trigPin, uint8_t echoPin);
void srf05_send_report(uint8_t trigPin, uint8_t echoPin, int distance);
void srf05_report_all();

void pin_send_report(uint8_t pin, int value);
void pin_report_all();

void setup() {
  Serial.begin(9600);
  WiFi.begin(ssid, password);

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  delay(1000);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiReconnect();
  }

  if (!client.connected()) {
    brokerReconnect();
  }

  client.loop();

  delay(1000);

  srf05_report_all();
  pin_report_all();
}

void brokerReconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    if (client.connect(userID)) {
      Serial.println("connected");

      for (int i = 0; i < array_len(subscribe_topics); i++) {
        String topic(String(userID) + '/' + subscribe_topics[i] + "/+");
        client.subscribe(topic.c_str());
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void wifiReconnect() {
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Attempting WiFi connection...");

    if (WiFi.reconnect()) {
      Serial.println("connected");
    } else {
      Serial.println("falied. try again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  char payload_char[length + 1];
  strncpy(payload_char, (char*)payload, length);
  payload_char[length] = '\0';

  struct TopicInfo topicInfo = parseTopic(topic);
  const char* part = topicInfo.part.c_str();
  const char* id = topicInfo.id.c_str();

  Component comp;
  try {
    comp = parseComponent(part);
  } catch (const std::invalid_argument& e) {
    // This Block does not execute. Idk why.
  }

  switch (comp) {
    case Component::PORT:
      {
        uint8_t port = atoi(id);

        if (strcmp(payload_char, "ON") == 0) {
          pinMode(port, OUTPUT);
          digitalWrite(atoi(id), HIGH);
        } else if (strcmp(payload_char, "OFF") == 0) {
          digitalWrite(port, LOW);
        }
        break;
      }
    case Component::PIN:
      {
        uint8_t pin = atoi(id);
        if (strcmp(payload_char, "ON") == 0) {
          if (std::find(subsribed_pins.begin(), subsribed_pins.end(), pin) == subsribed_pins.end()) {

            subsribed_pins.push_back(pin);

            pinMode(pin, INPUT);
          }
        } else if (strcmp(payload_char, "OFF") == 0) {
          subsribed_pins.erase(std::remove(subsribed_pins.begin(), subsribed_pins.end(), pin), subsribed_pins.end());
        } else if (strcmp(payload_char, "ONCE") == 0) {
          pinMode(pin, INPUT);

          int value = digitalRead(pin);

          pin_send_report(pin, value);
        }
        break;
      }
    case Component::SRF05:
      {
        if (strcmp(payload_char, "ON") == 0) {
          if (std::find(srf05_pins.begin(), srf05_pins.end(), id) == srf05_pins.end()) {
            srf05_pins.push_back(id);

            uint8_t trigPin, echoPin;
            sscanf(id, "%u,%u", &trigPin, &echoPin);
            pinMode(echoPin, INPUT);
            pinMode(trigPin, OUTPUT);
          }

        } else if (strcmp(payload_char, "OFF") == 0) {
          srf05_pins.erase(std::remove(srf05_pins.begin(), srf05_pins.end(), topicInfo.id), srf05_pins.end());
        } else if (strcmp(payload_char, "ONCE") == 0) {
          uint8_t trigPin, echoPin;
          sscanf(id, "%u,%u", &trigPin, &echoPin);
          pinMode(echoPin, INPUT);
          pinMode(trigPin, OUTPUT);

          int distance = srf05_distance(trigPin, echoPin);

          srf05_send_report(trigPin, echoPin, distance);
        }
        break;
      }
  }
}

struct TopicInfo parseTopic(char* topic) {
  struct TopicInfo topicInfo;
  const char delim[] = "/";

  strtok(topic, delim);
  String part = strdup(strtok(NULL, delim));
  String id = strdup(strtok(NULL, delim));

  return { part, id };
}

int srf05_distance(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH);
  int distance = duration / 29 / 2;

  return distance;
}

void srf05_send_report(uint8_t trigPin, uint8_t echoPin, int distance) {
  String payload(distance);
  String publishTopic(String(userID) + '/' + "SRF05" + '/' + trigPin + ',' + echoPin + "/out");

  client.publish(publishTopic.c_str(), payload.c_str());
}

void srf05_report_all() {
  for (String pins : srf05_pins) {
    uint8_t trigPin, echoPin;
    sscanf(pins.c_str(), "%u,%u", &trigPin, &echoPin);

    int distance = srf05_distance(trigPin, echoPin);

    srf05_send_report(trigPin, echoPin, distance);
  }
}

Component parseComponent(const char* str) {
  static const std::unordered_map<std::string_view, Component> componentMap = {
    { "PORT", Component::PORT },
    { "PIN", Component::PIN },
    { "SRF05", Component::SRF05 },
  };

  auto it = componentMap.find(str);
  if (it != componentMap.end()) {
    return it->second;
  } else {
    throw std::invalid_argument("Invalid name for component type.");
  }
}

void pin_send_report(uint8_t pin, int value) {
  String payload(value);
  String publishTopic(String(userID) + '/' + "PIN" + '/' + pin + "/out");

  client.publish(publishTopic.c_str(), payload.c_str());
}

void pin_report_all() {
  for (uint8_t pin : subsribed_pins) {
    int value = digitalRead(pin);

    pin_send_report(pin, value);
  }
}
