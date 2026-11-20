# Sistema Inteligente de Irrigação — ESP32 + Wokwi + Blynk

Arquivos desta pasta:

| Arquivo | O que é |
|---|---|
| `sketch.ino` | Código C++ do ESP32 (leitura do sensor, controle da bomba, comunicação com o Blynk) |
| `diagram.json` | Circuito do Wokwi (ESP32, potenciômetro, relé, LED da bomba) |
| `libraries.txt` | Biblioteca usada (Blynk) |

---

## 1. Como o sistema funciona

```
 Potenciômetro (sensor) ──► ESP32 ──► Relé ──► LED azul (bomba)
                              │
                     Wi-Fi "Wokwi-GUEST"
                              │
                       Blynk Cloud (TCP)
                              │
                      App Blynk no celular
```

- **Modo automático (sempre ligado):** umidade **abaixo de 30%** → liga a bomba; ao chegar em **60%** → desliga. A diferença entre os dois valores (histerese) evita que a bomba fique ligando e desligando sem parar.
- **Modo manual (pelo app):** o botão V1 força a bomba a ligar. Por segurança ele desliga sozinho se a umidade passar de **90%** ou depois de **30 segundos**, e o botão no app volta para OFF.
- **Sem internet:** o controle automático continua funcionando. Se o token do Blynk ainda for `SEU_TOKEN_AQUI`, o sistema roda em modo offline (bom para testar o circuito antes de configurar o Blynk).

Os valores ficam no topo do `sketch.ino` (`LIMITE_SECO`, `LIMITE_IDEAL`, `LIMITE_MAXIMO`, `TEMPO_MAX_MANUAL_MS`).

### Ligações

| Componente | Pino | ESP32 |
|---|---|---|
| Potenciômetro (sensor de umidade) | VCC / GND / SIG | 3V3 / GND / **GPIO 34** |
| Módulo relé | VCC / GND / IN | 5V / GND / **GPIO 26** |
| Relé → bomba | COM → 5V, NO → resistor 220 Ω → LED azul → GND | — |

---

## 2. Configurar o Blynk (uma vez)

1. Crie uma conta em **blynk.cloud** e entre no **Blynk.Console**.
2. **Developer Zone → My Templates → New Template**
   - Name: `Irrigacao Inteligente` · Hardware: `ESP32` · Connection: `WiFi`
3. Na aba **Datastreams**, crie 4 datastreams do tipo **Virtual Pin**:

   | Nome | Pino | Tipo | Mín | Máx | Unidade |
   |---|---|---|---|---|---|
   | Umidade | V0 | Integer | 0 | 100 | % |
   | Bomba manual | V1 | Integer | 0 | 1 | — |
   | Estado bomba | V2 | Integer | 0 | 1 | — |
   | Status | V3 | String | — | — | — |
   | Simular umidade | V4 | Integer | 0 | 100 | % |

   A umidade pode ser mudada pelo **potenciômetro do Wokwi** ou pelo **Slider V4** do Blynk: vale o último que foi mexido.

4. **Web Dashboard** (opcional, para mostrar no computador): Gauge → V0, Switch → V1, LED → V2, Label → V3, Slider → V4. Clique em **Save**.
5. **Mobile Dashboard** (no app Blynk IoT do celular, abra o template e edite):
   - **Gauge** → V0 (umidade)
   - **Button**, modo **Switch** → V1 (bomba manual)
   - **LED** → V2 (bomba ligada/desligada)
   - **Label** → V3 (mensagem de status)
   - **Slider** → V4 (simular umidade)
6. **Devices → New Device → From template** → escolha `Irrigacao Inteligente`.
7. Abra o device → **Device Info** e copie as 3 linhas:
   ```cpp
   #define BLYNK_TEMPLATE_ID   "TMPL..."
   #define BLYNK_TEMPLATE_NAME "Irrigacao Inteligente"
   #define BLYNK_AUTH_TOKEN    "..."
   ```

---

## 3. Montar no Wokwi

1. Acesse **wokwi.com/projects/new/esp32**.
2. Aba **sketch.ino** → apague tudo e cole o conteúdo do nosso `sketch.ino`.
3. Aba **diagram.json** → apague tudo e cole o nosso `diagram.json`.
4. Clique na setinha ao lado das abas → **New file…** → `libraries.txt` → cole o conteúdo (ou use o **Library Manager** e adicione `Blynk`).
5. No topo do `sketch.ino`, troque as 3 linhas `BLYNK_...` pelas que você copiou do Blynk.
6. Clique em **Save** (para gerar o link do projeto que vai na proposta) e depois em **▶ Start**.

No monitor serial deve aparecer algo assim:

```
=== Sistema Inteligente de Irrigacao ===
Conectando ao Wi-Fi ...... OK
>>> Conectado ao Blynk Cloud
Umidade:  45% | Bomba: OFF | Modo: automatico | Umidade OK | Blynk: online
```

> Dica: o link do Wokwi é público. Quem tiver o link vê o token do Blynk. Para a entrega tudo bem, mas não reutilize o token em outro projeto.

---

## 4. Roteiro de demonstração (apresentação)

1. **Start** na simulação → mostre o device ficando **Online** no app.
2. Gire o potenciômetro para a **esquerda** (abaixo de 30%) → o LED azul acende, o app mostra *"Solo seco - irrigando"* e o LED V2 acende.
3. Gire para a **direita** aos poucos (simulando a terra molhando) → em 60% a bomba desliga sozinha (*"Umidade ideal"*).
4. Com a umidade entre 30% e 60%, ligue o **botão V1** no celular → a bomba liga na hora (*"Irrigacao manual ativa"*).
5. Desligue o botão, ou espere 30 s, ou gire acima de 90% → a bomba desliga e o botão volta para OFF sozinho.

---

## 5. Problemas comuns

| Sintoma | Causa / solução |
|---|---|
| `Invalid auth token` no serial | Token copiado errado ou de outro device. Copie de novo do Device Info. |
| Device fica **Offline** no app | Confira se o `BLYNK_TEMPLATE_ID` é o mesmo do template do device. |
| Erro de compilação `BlynkSimpleEsp32.h: No such file` | Falta o `libraries.txt` com `Blynk`. |
| Gauge não se mexe | O Gauge precisa estar ligado ao datastream **V0**, com Máx = 100. |
| Botão não liga a bomba | O Button precisa estar em modo **Switch** e ligado ao **V1**. |
| Sensor real lê invertido | Troque `ADC_SECO` e `ADC_MOLHADO` no código (sensor capacitivo: seco = valor alto). |
