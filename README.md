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

### R2 — Decisão local (funciona sem internet)

A regra de irrigação roda **dentro do ESP32**, na função `controle()` do `sketch.ino`, e não depende do Blynk nem do Wi-Fi:

| Condição (lida no sensor local) | Ação no relé |
|---|---|
| umidade **< 30%** | liga a bomba |
| umidade **≥ 60%** | desliga a bomba |
| entre 30% e 60% | mantém o estado atual (histerese) |

O que garante que funciona offline:
- A regra é executada a cada 1 s pelo `timer.run()`, **antes** de qualquer código de rede no `loop()`.
- Se a internet cair, o ESP32 tenta reconectar ao Blynk só a cada 10 s e por no máximo 2 s, então a irrigação não trava.
- Sem conexão, o slider V4 do app deixa de valer e a umidade volta a vir do sensor local (potenciômetro).
- O monitor serial mostra `Blynk: OFFLINE (decisao local)` enquanto estiver sem conexão.

**Como demonstrar:** no topo do `sketch.ino`, troque `#define MODO_SEM_INTERNET false` para `true` e clique em **▶ Start**. O ESP32 nem tenta entrar no Wi-Fi. Gire o potenciômetro para a esquerda (abaixo de 30%) e a bomba (LED azul) liga; gire para a direita (60% ou mais) e ela desliga. No Blynk.Console o dispositivo aparece **Offline** o tempo todo, o que prova que a decisão foi tomada no próprio dispositivo. Volte para `false` depois.

Os valores ficam no topo do `sketch.ino` (`LIMITE_SECO`, `LIMITE_IDEAL`, `LIMITE_MAXIMO`, `TEMPO_MAX_MANUAL_MS`).

### Ligações

| Componente | Pino | ESP32 |
|---|---|---|
| Potenciômetro (sensor de umidade) | VCC / GND / SIG | 3V3 / GND / **GPIO 34** |
| Módulo relé | VCC / GND / IN | 5V / GND / **GPIO 26** |
| Relé → bomba | COM → 5V, NO → resistor 220 Ω → LED azul → GND | — |
