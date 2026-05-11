# ESP32 + RFM95W — LoRaWAN Node Base (OTAA · AU915 · ChirpStack)

> **Firmware base para nó LoRaWAN com ESP32 + RFM95W usando ativação OTAA, stack MCCI LMiC 3.0.99, plano de canais AU915 sub-banda 1 e servidor de rede ChirpStack.**  
> Inclui impressão das chaves de sessão após o JOIN, MIC no Serial e diagnósticos completos de sessão.

---

## Índice

- [Sobre o projeto](#sobre-o-projeto)
- [Hardware necessário](#hardware-necessário)
- [Mapeamento de pinos](#mapeamento-de-pinos)
- [Dependências de software](#dependências-de-software)
- [Instalação da biblioteca LMiC](#instalação-da-biblioteca-lmic)
- [Configuração das credenciais OTAA](#configuração-das-credenciais-otaa)
- [Plano de canais AU915 — sub-banda 1](#plano-de-canais-au915--sub-banda-1)
- [Como funciona o JOIN OTAA](#como-funciona-o-join-otaa)
- [Diagnósticos de sessão e MIC](#diagnósticos-de-sessão-e-mic)
- [Saída esperada no Serial Monitor](#saída-esperada-no-serial-monitor)
- [Cadastro no ChirpStack](#cadastro-no-chirpstack)
- [Diferenças em relação ao ABP](#diferenças-em-relação-ao-abp)
- [Problemas conhecidos e soluções](#problemas-conhecidos-e-soluções)
- [Estrutura do repositório](#estrutura-do-repositório)
- [Licença](#licença)

---

## Sobre o projeto

Este repositório contém o firmware base para um nó LoRaWAN construído com **ESP32** e módulo de rádio **RFM95W** (SX1276), operando com ativação **OTAA** (*Over-The-Air Activation*) no plano de canais **AU915, sub-banda 1** (canais 8–15 + 65).

O código é parte de um projeto maior de medição de energia elétrica em tempo real usando uma rede LoRaWAN ponta a ponta, com backend em **Java/Spring**, frontend em **TypeScript/Angular** e aplicativo móvel em **Dart/Flutter**. Este repositório isola a camada de firmware do nó sensor, servindo como ponto de partida para qualquer aplicação que precise enviar dados via LoRaWAN com ESP32 em modo OTAA.

### O que este firmware faz

- Inicia um procedimento de JOIN OTAA ao ligar, enviando um `JoinRequest` com AppEUI + DevEUI assinado com a AppKey.
- Após receber o `JoinAccept` do ChirpStack, imprime as chaves de sessão derivadas (DevAddr, NwkSKey, AppSKey) no Serial Monitor.
- Transmite um payload LoRaWAN a cada 10 segundos após o JOIN usando SF7/BW125 (DR5) com 14 dBm de potência.
- A cada transmissão (`EV_TXSTART`), imprime as chaves de sessão, DevAddr, frame raw, contadores FCnt e o **MIC** isolado.
- Recebe e exibe downlinks do servidor nas janelas RX1/RX2 (Class A).
- Configura corretamente o plano de canais AU915 sub-banda 1 e a janela RX2 (923.3 MHz / SF12/BW500).
- Desabilita o link-check mode em `setup()` **e** em `EV_JOINED`, pois o JOIN re-habilita automaticamente.

### Por que OTAA e não ABP?

| | OTAA | ABP |
|---|---|---|
| JOIN procedure | Sim — troca de chaves over-the-air | Não — sessão pré-provisionada |
| Chaves de sessão | Derivadas a cada JOIN (únicas por sessão) | Fixas no firmware |
| Segurança | **Maior** | Menor |
| Frame counter | Gerenciado automaticamente após JOIN | Requer persistência em NVS |
| Primeira transmissão | Após JOIN Accept | Imediata |
| Ideal para | **Produção** | Desenvolvimento e testes |

OTAA é o modo **recomendado para produção**. As chaves de sessão (DevAddr, NwkSKey, AppSKey) são derivadas pelo servidor a cada JOIN usando a AppKey — nunca trafegam pela rede em texto claro — e o frame counter é sincronizado automaticamente, eliminando o risco de `Frame-counter reset or rollover detected`.

---

## Hardware necessário

| Componente | Especificação | Observação |
|---|---|---|
| Microcontrolador | ESP32 (qualquer variante com 4 MB de flash) | Testado com ESP32-DevKitC |
| Módulo de rádio | RFM95W (HopeRF) — SX1276 | Frequência 915 MHz |
| Antena | 1/4 de onda para 915 MHz (~8,2 cm) | Obrigatória — nunca transmita sem antena |
| Cabo / conector | SMA ou u.FL dependendo do módulo | — |

### Diagrama de conexão ESP32 ↔ RFM95W

```
ESP32           RFM95W
─────────────────────────────────
GPIO 18 (SCLK) → SCK
GPIO 19 (MISO) → MISO
GPIO 23 (MOSI) → MOSI
GPIO  5 (NSS)  → NSS / CS
GPIO 14        → RESET
GPIO 34        → DIO0
GPIO 35        → DIO1
3.3 V          → VCC
GND            → GND
```

> ⚠️ **GPIOs 34, 35 e 39 no ESP32 são input-only** — não possuem resistor de pull-up interno. São adequados para DIO0 e DIO1 do RFM95W pois essas linhas são saídas do rádio (o ESP32 apenas lê). Nunca use esses pinos como saída.

---

## Mapeamento de pinos

Definido no sketch:

```cpp
#define PIN_SCLK    18
#define PIN_MISO    19
#define PIN_MOSI    23
#define PIN_NSS      5
#define PIN_RESET   14
#define PIN_DIO0    34
#define PIN_DIO1    35   // conectado mas não usado pelo LMiC nesta placa
#define PIN_DIO2    39   // não conectado — mantido como referência
```

Ajuste conforme sua montagem antes de compilar.

---

## Dependências de software

| Software | Versão | Link |
|---|---|---|
| Arduino IDE | 2.x | [arduino.cc/en/software](https://www.arduino.cc/en/software) |
| ESP32 Arduino core | 3.3.8 | [github.com/espressif/arduino-esp32](https://github.com/espressif/arduino-esp32) |
| MCCI LoRaWAN LMIC | **3.0.99** | Ver seção abaixo |

---

## Instalação da biblioteca LMiC

### Opção A — Via Arduino IDE Library Manager

> **Tools → Manage Libraries → pesquise "MCCI LoRaWAN LMIC" → instale a versão 3.0.99**

### Opção B — Via git

```bash
cd ~/Arduino/libraries

git clone --branch v3.0.99.10 \
  https://github.com/mcci-catena/arduino-lmic.git \
  MCCI_LoRaWAN_LMIC_library
```

### Correção obrigatória — conflito `hal_init` com ESP32 core 3.x

O ESP32 Arduino core 3.x define `hal_init` internamente, conflitando com a mesma função na LMiC. Corrija com dois comandos `sed`:

```bash
# Renomear hal_init no arquivo de implementação
sed -i 's/void hal_init(/void lmic_hal_init(/g' \
  ~/Arduino/libraries/MCCI_LoRaWAN_LMIC_library/src/hal/hal.cpp

# Atualizar as chamadas internas
sed -i 's/hal_init()/lmic_hal_init()/g' \
  ~/Arduino/libraries/MCCI_LoRaWAN_LMIC_library/src/lmic/lmic.c \
  ~/Arduino/libraries/MCCI_LoRaWAN_LMIC_library/src/hal/hal.cpp
```

Após isso, compile normalmente — o erro `multiple definition of 'hal_init'` não deve mais aparecer.

### Configuração da região no `lmic_project_config.h`

Edite o arquivo de configuração da biblioteca para habilitar apenas a região AU915:

```cpp
// ~/Arduino/libraries/MCCI_LoRaWAN_LMIC_library/project_config/lmic_project_config.h

#define CFG_au915 1
// #define CFG_eu868 1   // comentar
// #define CFG_us915 1   // comentar
// #define CFG_as923 1   // comentar

#define LMIC_USE_INTERRUPTS
```

---

## Configuração das credenciais OTAA

Em OTAA são necessárias três chaves: **AppEUI**, **DevEUI** e **AppKey**. Edite as constantes no topo do sketch:

```cpp
// AppEUI (JoinEUI) — 8 bytes, MSB first
static const u1_t PROGMEM APPEUI[8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // <-- substitua
};

// DevEUI — 8 bytes, MSB first
static const u1_t PROGMEM DEVEUI[8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // <-- substitua
};

// AppKey — 16 bytes, MSB first
static const u1_t PROGMEM APPKEY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // <-- substitua
};
```

### O que é cada chave

| Chave | Tamanho | Descrição |
|---|---|---|
| **AppEUI** (JoinEUI) | 8 bytes | Identificador da aplicação/servidor de join. Pode ser qualquer valor em desenvolvimento. |
| **DevEUI** | 8 bytes | Identificador único global do dispositivo. Deve ser único na rede. Use o EUI gravado no módulo ou gere um aleatório com prefixo OUI. |
| **AppKey** | 16 bytes | Chave raiz de derivação. **Nunca compartilhe** — dela são derivadas NwkSKey e AppSKey a cada JOIN. |

### Onde encontrar no ChirpStack

> Applications → sua application → Devices → seu device → **OTAA keys**

Os valores são exibidos em hexadecimal sem separadores. Para converter para o formato do array C:

```
ChirpStack DevEUI: DE631EA3E5F58DF3
Código C:          { 0xDE, 0x63, 0x1E, 0xA3, 0xE5, 0xF5, 0x8D, 0xF3 }
```

> ℹ️ O LMiC espera todos os valores em **MSB first** (big-endian), ao contrário de algumas outras bibliotecas que usam LSB first. O ChirpStack exibe os valores em MSB — copie diretamente sem inverter os bytes.

---

## Plano de canais AU915 — sub-banda 1

O AU915 define 72 canais de uplink organizados em 8 sub-bandas de 9 canais cada (8 × BW125 + 1 × BW500). Este firmware opera na **sub-banda 1** (canais 8–15 + 65), adotada como padrão pela comunidade brasileira e configurada no ChirpStack com `region_config = au915_1`.

| Canal | Frequência (MHz) | BW (kHz) | DR suportados | Uso |
|---|---|---|---|---|
| 8 | 916.8 | 125 | DR0–DR5 | Uplink dados |
| 9 | 917.0 | 125 | DR0–DR5 | Uplink dados |
| 10 | 917.2 | 125 | DR0–DR5 | Uplink dados |
| 11 | 917.4 | 125 | DR0–DR5 | Uplink dados |
| 12 | 917.6 | 125 | DR0–DR5 | Uplink dados |
| 13 | 917.8 | 125 | DR0–DR5 | Uplink dados |
| 14 | 918.0 | 125 | DR0–DR5 | Uplink dados |
| 15 | 918.2 | 125 | DR0–DR5 | Uplink dados |
| 65 | 917.5 | 500 | DR6 | **JOIN request** / ADR |
| **RX2** | **923.3** | **500** | **DR8 (SF12)** | **Downlink fixo** |

O canal 65 (BW500) é especialmente importante em OTAA — é nele que o `JoinRequest` é transmitido. Se esse canal estiver desabilitado, o JOIN nunca chega ao gateway.

O firmware configura o plano desabilitando todos os canais e habilitando apenas os da sub-banda 1:

```cpp
for (u1_t b  =  0; b  <  8; ++b)   LMIC_disableSubBand(b);
for (u1_t ch =  0; ch < 72; ++ch)  LMIC_disableChannel(ch);
for (u1_t ch =  8; ch <= 15; ++ch) LMIC_enableChannel(ch);  // BW125
LMIC_enableChannel(65);                                       // BW500 — JOIN

LMIC_setDrTxpow(DR_SF7, 14);      // DR5 = SF7/BW125, 14 dBm
LMIC.dn2Dr    = DR_SF12CR;        // RX2 = SF12/BW500 (DR8) — AU915
LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);  // 1% — ESP32 XTAL
```

---

## Como funciona o JOIN OTAA

O JOIN OTAA é um procedimento de autenticação mútua que estabelece uma sessão segura entre o dispositivo e o servidor de rede sem que as chaves de sessão trafeguem pela rede:

```
Dispositivo                                   ChirpStack
    │                                              │
    │── JoinRequest ──────────────────────────────>│
    │   ch 65, SF8/BW500 (DR6)                     │  AppEUI + DevEUI + DevNonce
    │   MIC = AES-128-CMAC(AppKey, JoinRequest)    │
    │                                              │
    │                                              │  Valida MIC com AppKey
    │                                              │  Gera DevNonce único
    │                                              │  Deriva: DevAddr
    │                                              │          NwkSKey = f(AppKey, AppNonce, NetID, DevNonce)
    │                                              │          AppSKey = f(AppKey, AppNonce, NetID, DevNonce)
    │                                              │
    │<── JoinAccept ───────────────────────────────│
    │   RX1 (1s) ou RX2 (2s, 923.3 MHz / SF12)    │  DevAddr + AppNonce + CFList
    │   Cifrado com AES-128-ECB(AppKey, ...)       │
    │                                              │
    │  Decifra JoinAccept com AppKey               │
    │  Deriva NwkSKey e AppSKey localmente         │
    │  Configura DevAddr e janelas RX              │
    │                                              │
    │── Uplink 1 (FCnt=0) ─────────────────────────│
    │   MIC = AES-128-CMAC(NwkSKey, ...)           │
```

**Pontos importantes:**

- O `JoinRequest` usa o **canal BW500 (ch 65, 917.5 MHz, SF8/DR6)** — por isso `LMIC_enableChannel(65)` é obrigatório.
- O `JoinAccept` pode chegar na janela **RX1** (1 s após o JoinRequest) ou **RX2** (2 s, 923.3 MHz / SF12).
- As chaves de sessão **NwkSKey** e **AppSKey** são derivadas localmente no dispositivo e no servidor a partir da AppKey — nunca trafegam pela rede.
- O **DevNonce** é incrementado a cada tentativa de JOIN para evitar replay attacks.
- O `CFList` no JoinAccept configura automaticamente os canais e a janela RX2 — o firmware define `LMIC.dn2Dr = DR_SF12CR` explicitamente como salvaguarda.
- Após o JOIN, o `seqnoUp` começa em 0 e é gerenciado automaticamente pelo LMiC — não é necessária persistência em NVS.

### Eventos do JOIN no Serial Monitor

| Evento | Significado |
|---|---|
| `EV_JOINING` | LMiC iniciou o procedimento de JOIN |
| `EV_JOIN_TXCOMPLETE: no JoinAccept` | JoinRequest enviado, nenhum JoinAccept recebido — LMiC vai retentar com backoff |
| `EV_JOIN_FAILED` | JOIN falhou permanentemente |
| `EV_JOINED` | JOIN aceito — sessão estabelecida, chaves impressas no Serial |

O LMiC retenta o JOIN automaticamente com backoff exponencial até receber o `JoinAccept`.

---

## Diagnósticos de sessão e MIC

Após o JOIN (`EV_JOINED`), o firmware imprime as chaves de sessão derivadas pelo ChirpStack e pelo dispositivo:

```
EV_JOINED
  netid:   0
  devaddr: 0x01A2B3C4
  AppSKey: FD-91-D5-5D-CE-3A-F1-02-30-93-E2-89-A1-D9-F3-DF
  NwkSKey: 15-1F-77-79-D7-AA-EE-42-2A-74-56-83-80-DB-70-45
```

Esses valores são obtidos via `LMIC_getSessionKeys()` diretamente do estado interno do LMiC após o JOIN — devem bater com os exibidos na aba **Activation** do device no ChirpStack.

A cada transmissão (`EV_TXSTART`), o firmware imprime:

- **NwkSKey** e **AppSKey** que o LMiC está usando internamente
- **DevAddr** em hex
- **Frame raw** completo em hex (MHDR + FHDR + FPort + FRMPayload + MIC)
- **FCnt** (seqnoUp e seqnoDn)
- **MIC** isolado — últimos 4 bytes do frame

O MIC pode ser comparado com o campo `mic` do JSON capturado no gateway ChirpStack (que exibe em decimal):

```bash
# Converter array decimal do JSON para hex
printf '%02X-%02X-%02X-%02X\n' 154 31 59 226
# → 9A-1F-3B-E2
```

---

## Saída esperada no Serial Monitor

Configure o Serial Monitor para **9600 baud**.

**Na inicialização — antes do JOIN:**

```
Starting — ChirpStack AU915 OTAA
5832: EV_JOINING
```

**A cada tentativa de JOIN sem resposta (LMiC retenta com backoff):**

```
12048: EV_JOIN_TXCOMPLETE: no JoinAccept
45312: EV_JOINING
52891: EV_JOIN_TXCOMPLETE: no JoinAccept
```

**Após JOIN aceito:**

```
98765: EV_JOINED
  netid:   0
  devaddr: 0x01A2B3C4
  AppSKey: FD-91-D5-5D-CE-3A-F1-02-30-93-E2-89-A1-D9-F3-DF
  NwkSKey: 15-1F-77-79-D7-AA-EE-42-2A-74-56-83-80-DB-70-45
```

**A cada uplink após o JOIN:**

```
112540: EV_TXSTART
LMiC NwkSKey: 151F7779D7AAEE422A74568380DB7045
LMiC AppSKey: FD91D55DCE3AF1023093E289A1D9F3DF
LMiC DevAddr: 0x1A2B3C4

Frame raw: 40 C4 B3 A2 01 00 01 00 01 A3 7C B1 22 FF 9C 0D 7C 6C 72 36 8E CD 9A 1F 3B E2
FCnt/seqnoUp: 1
FCnt/seqnoDn: 0
  MIC: 9A-1F-3B-E2
Packet queued.
175430: EV_TXCOMPLETE (includes waiting for RX windows)
```

---

## Cadastro no ChirpStack

### Device Profile (OTAA)

| Campo | Valor |
|---|---|
| Region | `AU915` |
| Region configuration | `au915_1` |
| MAC version | `LoRaWAN 1.0.3` |
| Regional parameters revision | `B` |
| Supports OTAA | ✅ Sim |
| Supports Class-C | ☐ Não |

### Cadastro do device

> Applications → sua application → **Add device**

| Campo | Valor |
|---|---|
| Name | nome descritivo (ex: `ESP32-OTAA-001`) |
| Device EUI | valor de `DEVEUI` no firmware |
| Device profile | perfil OTAA criado acima |

### OTAA Keys

Após salvar o device, acesse a aba **OTAA keys**:

| Campo | Valor |
|---|---|
| Application EUI (JoinEUI) | valor de `APPEUI` no firmware |
| Application key | valor de `APPKEY` no firmware |

### Verificar o JOIN no ChirpStack

Após gravar o firmware e ligar o dispositivo, verifique em:

> Devices → seu device → **Events**

Deve aparecer um evento `join` com status de sucesso. Na aba **Activation**, os campos DevAddr, NwkSKey e AppSKey são preenchidos automaticamente pelo ChirpStack após o JOIN — devem bater com os valores impressos no Serial Monitor.

---

## Diferenças em relação ao ABP

| Aspecto | OTAA (este repositório) | ABP |
|---|---|---|
| Credenciais no firmware | AppEUI + DevEUI + AppKey | DevAddr + NwkSKey + AppSKey |
| Chaves de sessão | Derivadas pelo servidor a cada JOIN | Fixas — as mesmas sempre |
| DevAddr | Atribuído pelo servidor no JoinAccept | Configurado manualmente |
| Frame counter | Zerado e sincronizado a cada JOIN | Requer persistência em NVS |
| `Preferences.h` | Não necessário | Necessário |
| `LMIC_setSession()` | Não usado | Obrigatório |
| `os_getArtEui/DevEui/DevKey` | Retornam as chaves reais | Stubs vazios (no-op) |
| `LMIC_setLinkCheckMode(0)` | Chamado em `setup()` **e** em `EV_JOINED` | Apenas em `setup()` |
| Segurança | **Maior** | Menor |
| Repositório equivalente | — | [esp32-lorawan-abp-au915-chirpstack](https://github.com/AdailSilva/esp32-lorawan-abp-au915-chirpstack) |

### Por que `LMIC_setLinkCheckMode(0)` é chamado duas vezes em OTAA

O JOIN Accept recebido do ChirpStack **re-habilita automaticamente** o link-check mode no LMiC. Se não for desabilitado novamente em `EV_JOINED`, o stack passa a incluir MAC commands de link-check em todo uplink, o que pode forçar o ADR a reduzir o data rate e diminuir o tamanho máximo do payload disponível.

```cpp
// setup() — desabilita antes do JOIN
LMIC_setLinkCheckMode(0);

// EV_JOINED — desabilita novamente após o JOIN re-habilitar
case EV_JOINED:
    Serial.println(F("EV_JOINED"));
    printSessionKeys();
    LMIC_setLinkCheckMode(0);  // obrigatório em OTAA
    break;
```

---

## Problemas conhecidos e soluções

### ❌ `multiple definition of 'hal_init'`

**Causa:** conflito entre ESP32 Arduino core 3.x e LMiC.

**Solução:** aplicar o patch `sed` descrito na seção [Instalação da biblioteca LMiC](#instalação-da-biblioteca-lmic).

---

### ❌ Loop de `EV_JOIN_TXCOMPLETE: no JoinAccept`

O dispositivo envia JoinRequests mas nunca recebe JoinAccept. O LMiC retenta indefinidamente com backoff exponencial.

**Causas possíveis e diagnóstico:**

| Causa | Como verificar | Solução |
|---|---|---|
| Sub-banda errada | Gateway escuta canais diferentes dos transmitidos | Confirmar `au915_1` no ChirpStack e `LMIC_enableChannel(8-15, 65)` no firmware |
| Canal 65 desabilitado | JoinRequest não alcança o gateway | Confirmar `LMIC_enableChannel(65)` no firmware |
| AppKey incorreta | JOIN chega ao ChirpStack mas é rejeitado nos Events | Conferir AppKey byte a byte no firmware e no ChirpStack |
| DevEUI não cadastrado | Nenhum evento nos Events do ChirpStack | Cadastrar o device com o DevEUI correto |
| Gateway offline | Nenhum frame no Live Frames do gateway | Verificar conectividade do gateway com o ChirpStack |

---

### ❌ JOIN aceito mas uplinks não chegam ao ChirpStack

**Causa mais comum:** Device Profile com `Supports Class-C → yes`. O ChirpStack envia downlinks com timing `"immediately"` em vez de aguardar RX1/RX2, e os uplinks podem ser associados a uma sessão de Class C que o firmware não implementa.

**Solução:** desabilite `Supports Class-C` no Device Profile e limpe a fila do device.

---

### ❌ Downlinks não chegam após o JOIN

**Causa:** janela RX2 não configurada corretamente. Em OTAA, o JoinAccept do ChirpStack configura via CFList, mas o firmware define explicitamente por segurança:

```cpp
LMIC.dn2Dr = DR_SF12CR;  // DR8 = SF12/BW500 — RX2 AU915 (923.3 MHz)
```

Confirme que o Device Profile tem `RX2 DR = 8` e `RX2 frequency = 923300000`.

---

### ❌ Payload muito grande — descartado pelo ChirpStack

O DR5 (SF7/BW125) permite no máximo **222 bytes** de payload útil na AU915. Verifique o tamanho antes de enviar:

```cpp
// sizeof(payload) - 1 desconta o '\0' do string literal
LMIC_setTxData2(1, payload, sizeof(payload) - 1, 0);
```

---

## Estrutura do repositório

```
.
├── LoRaWAN_Node_Base_OTAA/
│   └── LoRaWAN_Node_Base_OTAA.ino  ← sketch principal
├── .gitattributes
├── LICENSE
└── README.md
```

---

## Licença

MIT — veja [LICENSE](LICENSE) para detalhes.

---

**Autor:** Adail dos Santos Silva  
**E-mail:** adail101@hotmail.com  
**WhatsApp:** +55 89 9 9412-9256
