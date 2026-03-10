#include <WiFi.h>
#include <WebServer.h>
#include <coap-simple.h>
#include <arduinoJson.h>

// Configurações WiFi
const char* ssid = "";
const char* password = ""; //add suas credenciais aq

// Configurações UART 
#define SERIAL_MEGA Serial2  
#define RX_PIN 16               // GPIO 16 (RX2)
#define TX_PIN 17               // GPIO 17 (TX2)

// Servidor Web HTTP
WebServer server(80);

// Servidor CoAP
WiFiUDP udp;
Coap coap(udp);

// Structs para LED RGB
struct led {
  int luz;
  int pin;
  bool pisc;
  unsigned long ult_p;
};

struct ledRGB {
  led R;
  led G;
  led B;
  int est;
};

// Variáveis do sistema
struct PACOTE {
  float temperatura;
  float umidade;
  int luz;
  bool presenca;
  bool modoManual;
  bool irrigando;
  bool luzAuxiliar;
  int angulo;  // Ângulo do servo
} dados;

ledRGB rgb;
bool modoNoturno;
int modoEconomia = 0;

// funções para LED RGB
void STAR(int pinR, int pinG, int pinB) {
  rgb.R.pin = pinR;
  rgb.G.pin = pinG;
  rgb.B.pin = pinB;

  rgb.R.luz = 0;
  rgb.G.luz = 0;
  rgb.B.luz = 0;

  rgb.R.pisc = false;
  rgb.G.pisc = false;
  rgb.B.pisc = false;

  rgb.R.ult_p = 0;
  rgb.G.ult_p = 0;
  rgb.B.ult_p = 0;

  rgb.est = 0;

  pinMode(rgb.R.pin, OUTPUT);
  pinMode(rgb.G.pin, OUTPUT);
  pinMode(rgb.B.pin, OUTPUT);
}

void COR(int r, int g, int b) {
  rgb.R.luz = r;
  rgb.G.luz = g;
  rgb.B.luz = b;

  analogWrite(rgb.R.pin, r);
  analogWrite(rgb.G.pin, g);
  analogWrite(rgb.B.pin, b);
}

void PISCA_COR(int r, int g, int b, int vezes = 3) {
  for (int i = 0; i < vezes; i++) {
    COR(r, g, b);
    delay(200);
    COR(0, 0, 0);
    delay(200);
  }
}

void GEN_RGB() {

  if (dados.modoManual) {
    COR(255, 165, 0);  // Laranja
    return;
  }

  if (dados.irrigando) {
    int intensidade = map(modoEconomia, 0, 100, 255, 50);

    if (modoEconomia >= 100) {
      PISCA_COR(0, 0, intensidade);
    } else {
      COR(0, 0, intensidade);  // Azul
    }
    return;
  }

  COR(0, 255, 0);  // Verde
}

// funções uart

// lê dados do arduino 
void RECEBE() {
  if (SERIAL_MEGA.available()) {
    String linha = SERIAL_MEGA.readStringUntil('\n');
    linha.trim();

    // Se é notificação
    if (linha.startsWith("NOTIF:")) {
      String tipo = linha.substring(6);
      GEN_NOTF(tipo);
      return;
    }

    // Se é mensagem de debug, só mostra
    if (linha.startsWith("[")) {
      Serial.println("[arduino] " + linha);
      return;
    }

    // Se é JSON com dados
    if (linha.startsWith("{")) {
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, linha);

      if (error) {
        Serial.print("[UART] Erro ao parsear JSON: ");
        Serial.println(error.c_str());
        return;
      }

      dados.temperatura = doc["temp"] | 0.0;
      dados.umidade = doc["umid"] | 0.0;
      dados.luz = doc["luz"] | 0;
      dados.presenca = doc["pres"] | false;
      dados.modoManual = doc["manual"] | false;
      dados.irrigando = doc["irr"] | false;
      dados.luzAuxiliar = doc["laux"] | false;
      modoEconomia = doc["econ"] | 0;
      dados.angulo = doc["angulo"] | 0;
    }
  }
}

// processa notificação 
void GEN_NOTF(String tipo) {
  if (tipo == "TEMP") {
    PISCA_COR(255, 0, 0);  // Vermelho
    Serial.println("[NOTIF] Temperatura anormal");
  } else if (tipo == "LUZ") {
    PISCA_COR(255, 255, 0);  // Amarelo
    Serial.println("[NOTIF] Muita luz");
  } else if (tipo == "UMID") {
    PISCA_COR(0, 255, 255);  // Ciano
    Serial.println("[NOTIF] Umidade anormal");
  }
  
  // restaura led
  GEN_RGB();
}

// envia cmd para arduino
void ENVIA(String comando) {
  SERIAL_MEGA.println(comando);
  Serial.print("[UART] Enviado: ");
  Serial.println(comando);
}

// funções coap

void callback_info(CoapPacket& packet, IPAddress ip, int port) {
  String response = gerarJsonDados();
  coap.sendResponse(ip, port, packet.messageid, response.c_str());  
  Serial.println("[CoAP] GET /info requisitado");
}

void callback_comando(CoapPacket& packet, IPAddress ip, int port) {
  char payload[packet.payloadlen + 1];
  memcpy(payload, packet.payload, packet.payloadlen);
  payload[packet.payloadlen] = '\0';

  StaticJsonDocument<256> doc;
  deserializeJson(doc, payload);

  if (doc.containsKey("economia")) {
    modoEconomia = doc["economia"];
    modoEconomia = constrain(modoEconomia, 0, 100);
    ENVIA("ECON:" + String(modoEconomia));
  }

  if (doc.containsKey("modo_noturno")) {
    modoNoturno = doc["modo_noturno"];
    ENVIA(modoNoturno ? "NOTURNO:1" : "NOTURNO:0");
  }

  if (doc.containsKey("previsao_chuva")) {
    int prob = doc["previsao_chuva"];
    ENVIA("CHUVA:" + String(prob));
  }

  String response = "{\"status\":\"ok\"}";
  coap.sendResponse(ip, port, packet.messageid, response.c_str());
}

// funções http

String gerarJsonDados() {
  StaticJsonDocument<512> doc;

  doc["temperatura"] = dados.temperatura;
  doc["umidade"] = dados.umidade;
  doc["luz"] = dados.luz;
  doc["presenca"] = dados.presenca;
  doc["modoManual"] = dados.modoManual;
  doc["irrigando"] = dados.irrigando;
  doc["luzAuxiliar"] = dados.luzAuxiliar;
  doc["modoEconomia"] = modoEconomia;
  doc["modoNoturno"] = modoNoturno;
  doc["aguaDesligada"] = (modoEconomia >= 100);
  doc["angulo"] = dados.angulo;

  String response;
  serializeJson(doc, response);

  return response;
}

String gerarJsonCompacto() {
  StaticJsonDocument<256> doc;

  doc["temp"] = dados.temperatura;
  doc["umid"] = dados.umidade;
  doc["luz"] = dados.luz;
  doc["irr"] = dados.irrigando;
  doc["econ"] = modoEconomia;

  String response;
  serializeJson(doc, response);

  return response;
}

String SendHTML() {
  String html = "<!DOCTYPE html><html lang='pt-BR'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Hortinha 2.0</title>";
  html += "<style>";
  html += "* { margin: 0; padding: 0; box-sizing: border-box; }";
  html += "body { font-family: Arial, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; padding: 20px; transition: background 1s ease; }";
  html += ".container { max-width: 1200px; margin: 0 auto; }";
  html += "h1 { color: white; text-align: center; margin-bottom: 30px; text-shadow: 2px 2px 4px rgba(0,0,0,0.3); transition: color 0.5s ease; }";
  html += ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }";
  html += ".card { background: white; border-radius: 15px; padding: 25px; box-shadow: 0 10px 30px rgba(0,0,0,0.2); }";
  html += ".card h2 { color: #667eea; margin-bottom: 20px; border-bottom: 3px solid #667eea; padding-bottom: 10px; }";
  html += ".sensor-item { display: flex; justify-content: space-between; padding: 12px; margin: 10px 0; background: #f8f9fa; border-radius: 8px; }";
  html += ".sensor-label { font-weight: 600; color: #495057; }";
  html += ".sensor-value { font-size: 1.2em; font-weight: bold; color: #667eea; }";
  html += ".led-indicator { width: 60px; height: 60px; border-radius: 50%; margin: 20px auto; box-shadow: 0 0 20px; }";
  html += ".slider-container { margin: 20px 0; }";
  html += ".slider-container label { display: block; margin-bottom: 10px; font-weight: 600; }";
  html += "input[type='range'] { width: 100%; height: 8px; border-radius: 5px; background: #d3d3d3; }";
  html += ".slider-value { text-align: center; font-size: 1.5em; font-weight: bold; color: #667eea; margin-top: 10px; }";
  html += "button { background: #667eea; color: white; border: none; padding: 12px 30px; border-radius: 8px; font-size: 1em; cursor: pointer; width: 100%; margin-top: 10px; }";
  html += "button:hover { background: #764ba2; }";
  html += ".alert { padding: 15px; border-radius: 8px; margin: 10px 0; }";
  html += ".alert-danger { background: #f8d7da; color: #721c24; border-left: 4px solid #dc3545; }";
  html += ".alert-warning { background: #fff3cd; color: #856404; border-left: 4px solid #ffc107; }";
  html += "</style></head><body>";

  html += "<div class='container'>";
  html += "<h1> 🌱 Hortinha 2.0 </h1>";

  html += "<div id='alerts'></div>";

  html += "<div class='grid'>";

  html += "<div class='card'><h2>📊 Sensores Ambientais</h2>";
  html += "<div class='sensor-item'><span class='sensor-label'>🌡️ Temperatura</span><span class='sensor-value' id='temp'>--°C</span></div>";
  html += "<div class='sensor-item'><span class='sensor-label'>💧 Umidade</span><span class='sensor-value' id='umid'>--%</span></div>";
  html += "<div class='sensor-item'><span class='sensor-label'>☀️ Luminosidade</span><span class='sensor-value' id='luz'>--</span></div>";
  html += "<div class='sensor-item'><span class='sensor-label'>👤 Presença</span><span class='sensor-value' id='pres'>--</span></div>";
  html += "</div>";

  html += "<div class='card'><h2>⚙️ Estado do Sistema</h2>";
  html += "<div style='text-align: center;'><div id='led' class='led-indicator' style='background: gray;'></div>";
  html += "<p id='ledStatus' style='font-size: 1.2em; font-weight: 600;'>Sistema Iniciando...</p></div>";
  html += "<div class='sensor-item'><span class='sensor-label'>Modo</span><span class='sensor-value' id='modo'>--</span></div>";
  html += "<div class='sensor-item'><span class='sensor-label'>Irrigação</span><span class='sensor-value' id='irr'>--</span></div>";
  html += "<div class='sensor-item'><span class='sensor-label'>Ângulo Servo</span><span class='sensor-value' id='angulo'>--°</span></div>";
  html += "</div>";

  html += "<div class='card'><h2>💧 Controle de Água</h2>";
  html += "<div class='slider-container'><label>Modo de Economia de Água</label>";
  html += "<input type='range' id='economia' min='0' max='100' value='0' oninput='updateSlider()'>";
  html += "<div class='slider-value'><span id='econValue'>0</span>%</div>";
  html += "<p style='text-align: center; color: #6c757d; margin-top: 10px; font-size: 0.9em;'>0% = Plena | 100% = Sem Água</p></div>";
  html += "<button onclick='aplicarEconomia()'>Aplicar Configuração</button>";
  html += "<div class='sensor-item' style='margin-top: 20px;'><span class='sensor-label'>Status Água</span><span class='sensor-value' id='agua'>--</span></div>";
  html += "</div>";

  html += "</div></div>";

  html += "<script>";
  html += "function updateSlider() { document.getElementById('econValue').textContent = document.getElementById('economia').value; }";
  html += "function aplicarEconomia() {";
  html += "  var valor = document.getElementById('economia').value;";
  html += "  fetch('/setEconomia?value=' + valor).then(r => r.text()).then(d => { alert('Economia aplicada: ' + valor + '%'); });";
  html += "}";
  html += "function atualizar() {";
  html += "  fetch('/getData').then(r => r.json()).then(d => {";
  html += "    document.getElementById('temp').textContent = d.temperatura + '°C';";
  html += "    document.getElementById('umid').textContent = d.umidade + '%';";
  html += "    document.getElementById('luz').textContent = d.luz;";
  html += "    document.getElementById('pres').textContent = d.presenca ? '✅ Detectada' : '❌ Ausente';";
  html += "    document.getElementById('modo').textContent = d.modoManual ? '🔧 Manual' : '🤖 Automático';";
  html += "    document.getElementById('irr').textContent = d.irrigando ? '💦 Ativa' : '⏸️ Inativa';";
  html += "    document.getElementById('angulo').textContent = d.angulo + '°';";
  html += "    document.getElementById('econValue').textContent = d.modoEconomia;";
  html += "    document.getElementById('economia').value = d.modoEconomia;";

  // MODO NOTURNO - Muda cor de fundo com transição suave
  html += "    var body = document.body;";
  html += "    var titulo = document.querySelector('h1');";
  html += "    if(d.modoNoturno) {";
  html += "      body.style.background = 'linear-gradient(135deg, #2c3e50 0%, #34495e 100%)';";
  html += "      titulo.innerHTML = '🌙 Hortinha 2.0 - Modo Noturno';";
  html += "      titulo.style.color = '#ecf0f1';";
  html += "    } else {";
  html += "      body.style.background = 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)';";
  html += "      titulo.innerHTML = '🌱 Hortinha 2.0 - Monitoramento';";
  html += "      titulo.style.color = 'white';";
  html += "    }";

  html += "    var led = document.getElementById('led');";
  html += "    var status = document.getElementById('ledStatus');";
  html += "    if(d.modoManual) { led.style.background = 'orange'; status.textContent = '🔧 Modo Manual'; }";
  html += "    else if(d.irrigando) { led.style.background = 'blue'; status.textContent = '💦 Irrigando (' + d.angulo + '°)'; }";
  html += "    else { led.style.background = 'green'; status.textContent = '✅ Sistema OK'; }";
  html += "    var alerts = document.getElementById('alerts');";
  html += "    alerts.innerHTML = '';";
  html += "    if(d.aguaDesligada) alerts.innerHTML += \"<div class='alert alert-danger'>⚠️ ÁGUA DESLIGADA!</div>\";";
  html += "    if(d.temperatura > 35) alerts.innerHTML += \"<div class='alert alert-warning'>🌡️ Temperatura alta!</div>\";";
  html += "    if(d.umidade < 30) alerts.innerHTML += \"<div class='alert alert-warning'>💧 Umidade baixa!</div>\";";

  // Alerta visual do modo noturno
  html += "    if(d.modoNoturno) {";
  html += "      alerts.innerHTML += \"<div class='alert' style='background:#34495e;color:#ecf0f1;border-left:4px solid #95a5a6'>🌙 Modo Noturno Ativo</div>\";";
  html += "    }";

  html += "  });";
  html += "}";
  html += "setInterval(atualizar, 2000);";
  html += "atualizar();";
  html += "</script>";

  html += "</body></html>";

  return html;
}

void handleRoot() {
  server.send(200, "text/html", SendHTML());
}

void handleGetData() {
  String response = gerarJsonDados();
  server.send(200, "application/json", response);
}

void handleSetEconomia() {
  if (server.hasArg("value")) {
    modoEconomia = server.arg("value").toInt();
    modoEconomia = constrain(modoEconomia, 0, 100);
    ENVIA("ECON:" + String(modoEconomia));
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Parâmetro 'value' ausente");
  }
}

void setup() {
  Serial.begin(115200);                                    
  SERIAL_MEGA.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);  // arduino

  STAR(25, 26, 27);

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/getData", handleGetData);
  server.on("/setEconomia", handleSetEconomia);
  server.begin();

  coap.server(callback_info, "info");
  coap.server(callback_comando, "comando");
  coap.start();

  Serial.println("\n===========================================");
  Serial.println("HORTINHA 2.0 - ESP32 (UART)");
  Serial.println("===========================================");
  Serial.print("Dashboard HTTP: http://");
  Serial.println(WiFi.localIP());
  Serial.print("Servidor CoAP:  coap://");
  Serial.print(WiFi.localIP());
  Serial.println(":5683");
  Serial.println("===========================================\n");

  dados.temperatura = 0;
  dados.umidade = 0;
  dados.luz = 0;
  dados.presenca = false;
  dados.modoManual = false;
  dados.irrigando = false;
  dados.luzAuxiliar = false;
  dados.angulo = 0;
}

void loop() {
  server.handleClient();
  coap.loop();

  RECEBE();
  GEN_RGB();

  delay(10);
}