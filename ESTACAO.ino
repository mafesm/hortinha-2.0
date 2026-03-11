//a estação n consegue receber ou a hortinha n consegue enviar o json. n tive tempo de debugar corretamente para saber, então fica as funções para um projeto futuro :)

#include <WiFi.h>
#include <coap-simple.h>
#include <ArduinoJson.h>

// Configurações WiFi
const char* ssid = "";
const char* password = ""; //add suas credenciais aq


IPAddress servidor_hortinha(192, 168, 33, 152);  // altere se precisar
const int porta_coap = 5683;

WiFiUDP udp;
Coap coap(udp);

// dados recebidos da hortinha
struct PACOTE {
  float temperatura;
  float umidade;
  int luz;
  bool presenca;
  bool modoManual;
  bool irrigando;
  bool luzAuxiliar;
  int modoEconomia;
  bool modoNoturno;
  bool aguaDesligada;
  int angulo;
} dados;

bool noite = false;

// funções coap

void callback_resposta(CoapPacket& packet, IPAddress ip, int port) {
  char payload[packet.payloadlen + 1];
  memcpy(payload, packet.payload, packet.payloadlen);
  payload[packet.payloadlen] = '\0';

  Serial.println("\n[CoAP] Resposta recebida:");
  Serial.println(payload);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("[ERRO] Falha ao parsear JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // att local
  dados.temperatura = doc["temperatura"] | 0.0;
  dados.umidade = doc["umidade"] | 0.0;
  dados.luz = doc["luz"] | 0;
  dados.presenca = doc["presenca"] | false;
  dados.modoManual = doc["modoManual"] | false;
  dados.irrigando = doc["irrigando"] | false;
  dados.luzAuxiliar = doc["luzAuxiliar"] | false;
  dados.modoEconomia = doc["modoEconomia"] | 0;
  dados.modoNoturno = doc["modoNoturno"] | false;
  dados.aguaDesligada = doc["aguaDesligada"] | false;
  dados.angulo = doc["angulo"] | 0;

  //exibir_info(); 
}

void req_info() {
  Serial.println("\n[CoAP] Requisitando dados da Hortinha...");

  int msgid = coap.get(servidor_hortinha, porta_coap, "info");

  if (msgid > 0) {
    Serial.print("[CoAP] Mensagem enviada (ID: ");
    Serial.print(msgid);
    Serial.println(")");
  } else {
    Serial.println("[ERRO] Falha ao enviar requisição CoAP");
  }
}

void envia(String comando, JsonDocument& payload) {
  Serial.print("\n[CoAP] Enviando comando: ");
  Serial.println(comando);

  String json;
  serializeJson(payload, json);

  Serial.print("[JSON] ");
  Serial.println(json);

  int msgid = coap.put(servidor_hortinha, porta_coap, "comando",
                       (char*)json.c_str(), json.length());

  if (msgid > 0) {
    Serial.println("[CoAP] Comando enviado com sucesso");
  } else {
    Serial.println("[ERRO] Falha ao enviar comando");
  }
}

void exibir_info() {
  Serial.println("\n========== DADOS DA HORTINHA ==========");
  Serial.print("🌡️  Temperatura: ");
  Serial.print(dados.temperatura);
  Serial.println("°C");
  Serial.print("💧 Umidade: ");
  Serial.print(dados.umidade);
  Serial.println("%");
  Serial.print("☀️  Luminosidade: ");
  Serial.println(dados.luz);
  Serial.print("👤 Presença: ");
  Serial.println(dados.presenca ? "Detectada" : "Ausente");
  Serial.print("⚙️  Modo: ");
  Serial.println(dados.modoManual ? "Manual" : "Automático");
  Serial.print("💦 Irrigação: ");
  Serial.println(dados.irrigando ? "ATIVA" : "Inativa");
  Serial.print("💡 Luz Auxiliar: ");
  Serial.println(dados.luzAuxiliar ? "LIGADA" : "Desligada");
  Serial.print("🔄 Economia: ");
  Serial.print(dados.modoEconomia);
  Serial.println("%");
  Serial.print("🌙 Modo Noturno: ");
  Serial.println(dados.modoNoturno ? "ATIVO" : "Inativo");
  Serial.print("💧 Água: ");
  Serial.println(dados.aguaDesligada ? "⚠️ DESLIGADA" : "Normal");
  Serial.print("📐 Ângulo Servo: ");
  Serial.print(dados.angulo);
  Serial.println("°");
  Serial.println("=======================================\n");
}


void gen_noite() {

  noite = !noite;
  StaticJsonDocument<128> doc;

  if (noite) {
    Serial.println("\n🌙 ");
    doc["modo_noturno"] = true;
  } else {
    Serial.println("\n☀️ ");
    doc["modo_noturno"] = false;
  }

  envia("MODO_NOTURNO", doc);
}

void gen_chuva(float probabilidade) {
  Serial.println("\n🌧️ ");

  StaticJsonDocument<128> doc;
  doc["previsao_chuva"] = (int)probabilidade;

  envia("PREVISAO_CHUVA", doc);
}

void gen_agua(int nivel) {
  Serial.print("\n💧 ");
  Serial.print(nivel);
  Serial.println("%");

  StaticJsonDocument<128> doc;
  doc["economia"] = nivel;

  envia("ECONOMIA", doc);
}

void comandos() {
  if (!Serial.available()) return;

  String comando = Serial.readStringUntil('\n');
  comando.trim();
  comando.toUpperCase();

  if (comando == "INFO" || comando == "STATUS") {
    req_info();
  } else if (comando == "NOTURNO") {
    gen_noite();
  } else if (comando.startsWith("CHUVA ")) {
    int prob = comando.substring(6).toInt();
    gen_chuva(prob);
  } else if (comando.startsWith("ECON ")) {
    int nivel = comando.substring(5).toInt();
    gen_agua(nivel);
  }
}


void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n");
  Serial.println("===========================================");
  Serial.println("   ESTAÇÃO");
  Serial.println("===========================================");

  // Conecta ao WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConectado ao WiFi!");
  Serial.print("Local: ");
  Serial.println(WiFi.localIP());
  Serial.print("Hortinha: ");
  Serial.println(servidor_hortinha);
  Serial.println("===========================================\n");

  // Configura CoAP
  coap.response(callback_resposta);
  coap.start();

  Serial.println("Cliente CoAP iniciado!");

  // primeira req antes de tudo
  delay(2000);
  req_info();
}

void loop() {
  coap.loop();

  comandos();

  delay(10);
}