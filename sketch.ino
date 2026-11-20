/*
 * =====================================================================
 *  Sistema Inteligente de Irrigação de Plantas Baseado em Umidade do Solo
 *  ESP32 + Blynk IoT  —  simulação no Wokwi
 *
 *  Sensor de umidade capacitivo -> potenciômetro (GPIO 34)
 *  Módulo relé 1 canal          -> GPIO 26 (aciona a bomba)
 *  Bomba d'água 5V              -> LED azul ligado pelo contato NO do relé
 *
 *  Blynk (pinos virtuais):
 *    V0 -> Umidade do solo (%)            [ESP32 -> app]  Gauge
 *    V1 -> Bomba manual (0/1)             [app -> ESP32]  Button (Switch)
 *    V2 -> Estado da bomba (0/1)          [ESP32 -> app]  LED
 *    V3 -> Mensagem de status (texto)     [ESP32 -> app]  Label
 *    V4 -> Simular umidade (0-100 %)      [app -> ESP32]  Slider
 *
 *  A umidade pode vir do potenciômetro do Wokwi OU do slider V4 do app:
 *  vale o último que foi mexido.
 * =====================================================================
 */

// ---------------------------------------------------------------------
// 1) CREDENCIAIS DO BLYNK
//    Copie do Blynk.Console > Devices > (seu device) > Device Info.
//    Enquanto o token for "SEU_TOKEN_AQUI" o sistema roda em modo
//    OFFLINE (irrigação automática funciona, só não envia para o app).
// ---------------------------------------------------------------------
#define BLYNK_TEMPLATE_ID   "TMPL2BDJr2nkf"
#define BLYNK_TEMPLATE_NAME "Irrigacao Inteligente"
#define BLYNK_AUTH_TOKEN    "SEU_TOKEN_AQUI"

#define BLYNK_PRINT Serial   // mostra os logs do Blynk no monitor serial

// ---------------------------------------------------------------------
// R2 - DECISÃO LOCAL: para demonstrar o sistema SEM INTERNET, troque
// para true. O ESP32 não conecta no Wi-Fi nem no Blynk e continua
// irrigando sozinho com base no potenciômetro (sensor de umidade).
// ---------------------------------------------------------------------
#define MODO_SEM_INTERNET false

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

// ---------------------------------------------------------------------
// 2) REDE — gateway virtual do Wokwi (sem senha)
// ---------------------------------------------------------------------
const char WIFI_SSID[] = "Wokwi-GUEST";
const char WIFI_PASS[] = "";
const int  WIFI_CANAL  = 6;   // canal fixo do Wokwi: conecta mais rápido

// ---------------------------------------------------------------------
// 3) PINOS
// ---------------------------------------------------------------------
const int PINO_SENSOR = 34;   // entrada analógica (somente leitura)
const int PINO_RELE   = 26;   // saída para o IN do módulo relé

// ---------------------------------------------------------------------
// 4) CALIBRAÇÃO DO SENSOR (ADC do ESP32: 0 a 4095)
//    No Wokwi: potenciômetro todo à esquerda = solo seco (0%),
//              todo à direita  = solo encharcado (100%).
//    Sensor capacitivo REAL é invertido (seco = valor alto):
//    meça o valor no ar e dentro d'água e troque os números abaixo.
// ---------------------------------------------------------------------
const int ADC_SECO    = 0;
const int ADC_MOLHADO = 4095;

// ---------------------------------------------------------------------
// 5) REGRAS DE IRRIGAÇÃO (%)
// ---------------------------------------------------------------------
const int LIMITE_SECO   = 30;  // abaixo disso: liga a bomba (automático)
const int LIMITE_IDEAL  = 60;  // ao atingir:   desliga a bomba (automático)
const int LIMITE_MAXIMO = 90;  // trava de segurança do modo manual

const unsigned long TEMPO_MAX_MANUAL_MS = 30000UL;  // manual desliga após 30 s
const unsigned long INTERVALO_LEITURA_MS = 1000UL;  // lê e envia a cada 1 s

// Pinos virtuais do Blynk
#define VP_UMIDADE V0
#define VP_MANUAL  V1
#define VP_BOMBA   V2
#define VP_STATUS  V3
#define VP_SLIDER  V4

// Quanto o potenciômetro precisa mudar (%) para voltar a ser a fonte
const int TOLERANCIA_POT = 3;

// ---------------------------------------------------------------------
// Estado do sistema
// ---------------------------------------------------------------------
BlynkTimer timer;

const bool usarBlynk = !MODO_SEM_INTERNET &&
                       (strcmp(BLYNK_AUTH_TOKEN, "SEU_TOKEN_AQUI") != 0);

const unsigned long INTERVALO_RECONEXAO_MS = 10000UL;  // tenta o Blynk a cada 10 s
unsigned long ultimaTentativaBlynk = 0;

int  umidade        = 0;
bool bombaLigada    = false;
bool irrigandoAuto  = false;   // pedido do controle automático
bool manualAtivo    = false;   // pedido do botão no app
unsigned long inicioManual = 0;
String ultimoStatus = "";

int  umidadeSlider  = -1;      // -1 = slider não está em uso
int  ultimoPot      = -1;      // última leitura do potenciômetro
int  ultimoEnviadoSlider = -1;

// ---------------------------------------------------------------------
// Funções auxiliares
// ---------------------------------------------------------------------
bool blynkOnline() {
  return usarBlynk && Blynk.connected();
}

// Lê o sensor (média de 10 amostras) e converte para 0-100 %
int lerUmidade() {
  long soma = 0;
  for (int i = 0; i < 10; i++) {
    soma += analogRead(PINO_SENSOR);
    delay(2);
  }
  int bruto = soma / 10;
  int pct = map(bruto, ADC_SECO, ADC_MOLHADO, 0, 100);
  return constrain(pct, 0, 100);
}

void setBomba(bool ligar) {
  if (ligar == bombaLigada) return;          // nada mudou
  bombaLigada = ligar;
  digitalWrite(PINO_RELE, ligar ? HIGH : LOW);
  Serial.printf(">>> BOMBA %s\n", ligar ? "LIGADA" : "DESLIGADA");
  if (blynkOnline()) Blynk.virtualWrite(VP_BOMBA, ligar ? 1 : 0);
}

void desligarManual(const char *motivo) {
  manualAtivo = false;
  Serial.printf(">>> Modo manual encerrado: %s\n", motivo);
  if (blynkOnline()) Blynk.virtualWrite(VP_MANUAL, 0);  // volta o botão do app
}

void enviarStatus(const String &status) {
  if (status == ultimoStatus) return;        // só envia quando muda
  ultimoStatus = status;
  if (blynkOnline()) Blynk.virtualWrite(VP_STATUS, status);
}

// ---------------------------------------------------------------------
// Ciclo principal de controle (roda a cada INTERVALO_LEITURA_MS)
// ---------------------------------------------------------------------
void controle() {
  // Fonte da umidade: potenciômetro (Wokwi) ou slider V4 (Blynk)
  int pot = lerUmidade();
  if (umidadeSlider >= 0 && ultimoPot >= 0 && abs(pot - ultimoPot) > TOLERANCIA_POT) {
    umidadeSlider = -1;   // mexeram no potenciômetro: ele volta a mandar
    Serial.println(">>> Fonte da umidade: potenciometro");
  }
  if (umidadeSlider >= 0 && !blynkOnline()) {
    umidadeSlider = -1;   // sem internet o slider não vale: volta o sensor local
    Serial.println(">>> Sem conexao: umidade volta a vir do potenciometro");
  }
  ultimoPot = pot;
  umidade = (umidadeSlider >= 0) ? umidadeSlider : pot;

  // Mantém o slider do app acompanhando o potenciômetro
  if (umidadeSlider < 0 && umidade != ultimoEnviadoSlider && blynkOnline()) {
    Blynk.virtualWrite(VP_SLIDER, umidade);
    ultimoEnviadoSlider = umidade;
  }

  // =================================================================
  // R2 - DECISÃO LOCAL (roda no próprio ESP32, não depende da internet)
  //   SE umidade < 30%            -> liga a bomba
  //   SE umidade >= 60%           -> desliga a bomba
  //   entre 30% e 60%             -> mantém como está (histerese)
  // =================================================================
  if (!irrigandoAuto && umidade < LIMITE_SECO) {
    irrigandoAuto = true;
  } else if (irrigandoAuto && umidade >= LIMITE_IDEAL) {
    irrigandoAuto = false;
  }

  // Travas de segurança do modo manual
  if (manualAtivo) {
    if (umidade >= LIMITE_MAXIMO) {
      desligarManual("solo encharcado");
    } else if (millis() - inicioManual >= TEMPO_MAX_MANUAL_MS) {
      desligarManual("tempo maximo atingido");
    }
  }

  setBomba(irrigandoAuto || manualAtivo);

  // Mensagem de status para o app
  String status;
  if (manualAtivo)                 status = "Irrigacao manual ativa";
  else if (irrigandoAuto)          status = "Solo seco - irrigando";
  else if (umidade >= LIMITE_MAXIMO) status = "Solo encharcado";
  else if (umidade >= LIMITE_IDEAL)  status = "Umidade ideal";
  else                             status = "Umidade OK";

  // Telemetria
  if (blynkOnline()) Blynk.virtualWrite(VP_UMIDADE, umidade);
  enviarStatus(status);

  Serial.printf("Umidade: %3d%% (%s) | Bomba: %-3s | Modo: %-10s | %s | Blynk: %s\n",
                umidade,
                umidadeSlider >= 0 ? "slider" : "pot",
                bombaLigada ? "ON" : "OFF",
                manualAtivo ? "manual" : "automatico",
                status.c_str(),
                blynkOnline() ? "online" : "OFFLINE (decisao local)");
}

// ---------------------------------------------------------------------
// Eventos do Blynk
// ---------------------------------------------------------------------

// Ao (re)conectar: sincroniza o app com o estado atual do ESP32
BLYNK_CONNECTED() {
  Serial.println(">>> Conectado ao Blynk Cloud");
  ultimoStatus = "";                           // força reenviar o status
  Blynk.virtualWrite(VP_MANUAL, manualAtivo ? 1 : 0);
  Blynk.virtualWrite(VP_BOMBA, bombaLigada ? 1 : 0);
  Blynk.virtualWrite(VP_UMIDADE, umidade);
  Blynk.virtualWrite(VP_SLIDER, umidade);
  ultimoEnviadoSlider = umidade;
}

// Slider "Simular umidade" no app (V4)
BLYNK_WRITE(VP_SLIDER) {
  umidadeSlider = constrain(param.asInt(), 0, 100);
  ultimoEnviadoSlider = umidadeSlider;
  Serial.printf(">>> Umidade definida pelo app: %d%%\n", umidadeSlider);
  controle();   // aplica na hora
}

// Botão "Bomba manual" no app (V1)
BLYNK_WRITE(VP_MANUAL) {
  int valor = param.asInt();

  if (valor == 1) {
    if (umidade >= LIMITE_MAXIMO) {
      Serial.println(">>> Pedido manual recusado: solo ja encharcado");
      Blynk.virtualWrite(VP_MANUAL, 0);
      ultimoStatus = "";
      enviarStatus("Recusado: solo encharcado");
      return;
    }
    manualAtivo = true;
    inicioManual = millis();
    Serial.println(">>> Comando do app: LIGAR bomba (manual)");
  } else {
    manualAtivo = false;
    Serial.println(">>> Comando do app: DESLIGAR bomba (manual)");
  }
  controle();   // responde na hora, sem esperar o próximo ciclo
}

// ---------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("=== Sistema Inteligente de Irrigacao ===");

  pinMode(PINO_RELE, OUTPUT);
  digitalWrite(PINO_RELE, LOW);
  analogReadResolution(12);

  if (usarBlynk) {
    Serial.print("Conectando ao Wi-Fi ");
    WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(250);
      Serial.print(".");
    }
    Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " FALHOU (seguindo offline)");
    Blynk.config(BLYNK_AUTH_TOKEN);   // a conexão é feita no loop()
  } else {
    Serial.println("Modo SEM INTERNET: so a decisao local esta ativa.");
  }

  timer.setInterval(INTERVALO_LEITURA_MS, controle);
  controle();
}

void loop() {
  // R2: a decisão local roda SEMPRE, com ou sem internet
  timer.run();

  if (!usarBlynk) return;

  if (Blynk.connected()) {
    Blynk.run();
  } else if (WiFi.status() == WL_CONNECTED &&
             millis() - ultimaTentativaBlynk >= INTERVALO_RECONEXAO_MS) {
    // Tentativa curta (2 s) para não travar a irrigação se a internet cair
    ultimaTentativaBlynk = millis();
    Serial.println("Tentando conectar ao Blynk...");
    Blynk.connect(2000);
  }
}
