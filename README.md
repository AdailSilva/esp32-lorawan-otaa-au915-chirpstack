# ESP32 + RFM95W — LoRaWAN Node Base (OTAA · AU915 · ChirpStack)

> **Firmware base para nó LoRaWAN com ESP32 + RFM95W usando ativação OTAA, stack MCCI LMiC 6.0.1, plano de canais AU915 sub-banda 1 e servidor de rede ChirpStack.**  
> Inclui impressão das chaves de sessão após o JOIN, MIC no Serial, diagnósticos completos de sessão e **reset remoto via downlink** na FPort 101.

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
- [Reset remoto via downlink — FPort 101](#reset-remoto-via-downlink--fport-101)
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
- **Processa comandos de reset remoto** recebidos na FPort 101 com payload de 8 bytes estruturado (header + command + tail).
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
| MCCI LoRaWAN LMIC | **6.0.1** | Ver seção abaixo |

---

## Instalação da biblioteca LMiC

### Opção A — Repositório com a versão correta (recomendado)

```bash
cd ~/Arduino/libraries

git clone \
  https://github.com/AdailSilva/IBM-LMIC-LoRaWAN-MAC-in-C-library-v6.0.1.git \
  MCCI_LoRaWAN_LMIC_library
```

### Opção B — Repositório upstream com tag v6.0.1

```bash
cd ~/Arduino/libraries

git clone --branch v6.0.1 \
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

## Reset remoto via downlink — FPort 101

O firmware implementa um protocolo de **reset remoto** que permite reiniciar o ESP32 enviando um downlink específico pelo ChirpStack. Isso é útil em campo, quando o dispositivo está instalado em local de difícil acesso e precisa ser reiniciado para se reconectar ou limpar algum estado interno.

### Protocolo do comando

O comando de reset usa uma porta e estrutura de payload dedicados para evitar resets acidentais por qualquer downlink genérico.

| Campo | Valor |
|---|---|
| **FPort** | `101` (0x65) |
| **Tamanho do payload** | `8 bytes` exatos |

**Estrutura do payload (8 bytes):**

```
Byte:   0     1     2     3     4     5     6     7
       ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
       │ HDR │ HDR │ CMD │ CMD │ CMD │ CMD │TAIL │TAIL │
       │0xAD │0xA1 │0xDE │0xAD │0xBE │0xEF │0x0D │0x0A │
       └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
         \_______/   \___________________________/  \___/
          Header             Command               Tail
         "AdAil"           Magic word             CRLF
```

| Segmento | Bytes | Valor | Significado |
|---|---|---|---|
| **Header** | 0–1 | `0xAD 0xA1` | Assinatura "**Ad**ail Silv**a1**" |
| **Command** | 2–5 | `0xDE 0xAD 0xBE 0xEF` | Magic word de reset (DEADBEEF) |
| **Tail** | 6–7 | `0x0D 0x0A` | Marcador de fim de frame (CRLF) |

**Representação em Base64** (necessário para enviar via MQTT/ChirpStack):

```bash
# Gerar o Base64 do comando de reset
printf '\xAD\xA1\xDE\xAD\xBE\xEF\x0D\x0A' | base64
# → rZHe rb4P Cg==  (pode variar por quebra de linha — use o valor contínuo abaixo)

# Valor Base64 direto para usar no ChirpStack / MQTT:
echo -n "raHerb7vDQo=" | base64 -d | xxd   # verificação
```

O valor Base64 correto para o payload completo é: **`raHerb7vDQo=`**

### Regras de validação no firmware

O firmware verifica **todos** os bytes antes de executar o reset:

```cpp
static bool isResetCommand(const uint8_t *data, uint8_t len) {
    if (len != RESET_CMD_LEN) return false;  // tamanho exato: 8 bytes

    return (data[0] == 0xAD &&   // Header byte 0
            data[1] == 0xA1 &&   // Header byte 1
            data[2] == 0xDE &&   // Command byte 0
            data[3] == 0xAD &&   // Command byte 1
            data[4] == 0xBE &&   // Command byte 2
            data[5] == 0xEF &&   // Command byte 3
            data[6] == 0x0D &&   // Tail byte 0 (CR)
            data[7] == 0x0A);    // Tail byte 1 (LF)
}
```

- FPort diferente de 101 → downlink exibido normalmente, comando ignorado.
- Tamanho diferente de 8 bytes → comando ignorado.
- Qualquer byte divergente → comando ignorado, payload esperado impresso no Serial.
- Payload correto → reset executado após `2000 ms` de delay (para `Serial.flush()` concluir).

### Por que o delay de 2 segundos antes do reset

O `esp_restart()` é chamado após `delay(RESET_DELAY_MS)` para garantir que:
1. O `Serial.flush()` termine de enviar os logs de diagnóstico.
2. Se o downlink for **confirmado** (`confirmed: true`), o ACK ainda seja transmitido pelo LMiC antes do reboot — embora em Class A isso não seja garantido pois o ACK vai no próximo uplink.

### Como disparar o reset pelo MQTT

**ChirpStack v4 — com TLS:**

```bash
APP_ID="56560a65-2fb8-444c-a1ee-bd0ee4be0946"
DEV_EUI="de631ea3e5f58df3"

mosquitto_pub \
  -h chirpstack-v4.adailsilva.com.br \
  -p 8883 \
  --cafile ~/chirpstack/mqtt-certs/ca.crt \
  -u "adailsilva" \
  -P "H@cker101" \
  -t "application/${APP_ID}/device/${DEV_EUI}/command/down" \
  -m '{
    "devEui": "de631ea3e5f58df3",
    "confirmed": false,
    "fPort": 101,
    "data": "raHerb7vDQo="
  }'
```

**Via interface web do ChirpStack:**

> Devices → seu device → **Queue** → Add item
> - FPort: `101`
> - Confirmed: `false`
> - Base64 data: `raHerb7vDQo=`

O downlink ficará na fila até o próximo uplink do dispositivo (Class A). Após receber, o ESP32 reinicia em ~2 segundos.

### Saída no Serial Monitor — reset bem-sucedido

```
159750: EV_TXCOMPLETE (includes waiting for RX windows)
  Downlink received — 8 byte(s) on FPort 101: AD A1 DE AD BE EF 0D 0A
  [RESET PORT] Command received on FPort 101 — validating...
  [RESET] Valid reset command — rebooting in 2 s...

ets Jun  8 2016 00:22:57
rst:0xc (SW_CPU_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
...
Starting — ChirpStack AU915 OTAA
5832: EV_JOINING
```

### Saída no Serial Monitor — payload inválido

```
159750: EV_TXCOMPLETE (includes waiting for RX windows)
  Downlink received — 8 byte(s) on FPort 101: AD A1 DE AD BE EF 0D FF
  [RESET PORT] Command received on FPort 101 — validating...
  [RESET] Invalid payload — command ignored.
  Expected: AD A1 DE AD BE EF 0D 0A
  Received: AD A1 DE AD BE EF 0D FF
```

### Personalização do protocolo

Todos os bytes do comando são definidos como constantes no topo do sketch — fácil de alterar sem tocar na lógica:

```cpp
#define RESET_FPORT      101    // FPort dedicada ao comando
#define RESET_CMD_LEN      8    // tamanho exato do payload
#define RESET_HDR_0     0xAD    // Header byte 0
#define RESET_HDR_1     0xA1    // Header byte 1
#define RESET_CMD_0     0xDE    // Command byte 0 (DEADBEEF)
#define RESET_CMD_1     0xAD    // Command byte 1
#define RESET_CMD_2     0xBE    // Command byte 2
#define RESET_CMD_3     0xEF    // Command byte 3
#define RESET_TAIL_0    0x0D    // Tail byte 0 — CR
#define RESET_TAIL_1    0x0A    // Tail byte 1 — LF
#define RESET_DELAY_MS  2000    // delay antes do esp_restart()
```

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

### Cálculo manual do MIC

O MIC é calculado com **AES-128-CMAC** sobre o frame completo precedido de um bloco B0 de 16 bytes:

```
MIC = AES-128-CMAC(NwkSKey, B0 | msg)[0:4]

B0  = 0x49 | 0x00×4 | Dir=0 | DevAddr(LE,4) | FCnt(LE,4) | 0x00 | len(msg)
msg = MHDR | DevAddr(LE,4) | FCtrl | FCnt(LE,2) | FPort | FRMPayload
```

> ℹ️ Em OTAA as chaves de sessão (NwkSKey e AppSKey) são derivadas após o JOIN. Use os valores impressos no Serial Monitor em `EV_JOINED` para o cálculo — eles devem bater com os exibidos na aba **Activation** do device no ChirpStack.

**Python** (`pip install pycryptodome`):

```python
from Crypto.Hash import CMAC
from Crypto.Cipher import AES
import struct

def calcular_mic(nwkskey_hex, devaddr_hex, fcnt, frame_sem_mic_hex):
    nwkskey    = bytes.fromhex(nwkskey_hex)
    devaddr_le = bytes.fromhex(devaddr_hex)[::-1]   # big-endian → little-endian
    msg        = bytes.fromhex(frame_sem_mic_hex)

    b0 = (bytes([0x49, 0x00, 0x00, 0x00, 0x00, 0x00])
          + devaddr_le
          + struct.pack('<I', fcnt)
          + bytes([0x00, len(msg)]))

    c = CMAC.new(nwkskey, ciphermod=AES)
    c.update(b0 + msg)
    return c.digest()[:4].hex()

# Exemplo — substitua pelos valores reais obtidos após o JOIN
mic = calcular_mic(
    nwkskey_hex       = "151f7779d7aaee422a74568380db7045",  # NwkSKey derivada no JOIN
    devaddr_hex       = "01a2b3c4",                          # DevAddr atribuído pelo ChirpStack
    fcnt              = 1,                                   # FCnt do uplink
    frame_sem_mic_hex = "40C4B3A20100010001..."              # Frame raw sem os 4 bytes de MIC
)
print(f"MIC: {mic}")
```

**Java** (sem dependências externas — usa `javax.crypto` da JDK):

```java
import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.HexFormat;

public class LoRaWANMic {

    public static String calcularMic(String nwksKeyHex,
                                     String devAddrHex,
                                     int fcnt,
                                     String frameWithoutMicHex) throws Exception {
        HexFormat hex = HexFormat.of();
        byte[] nwksKey   = hex.parseHex(nwksKeyHex);
        byte[] devAddrBe = hex.parseHex(devAddrHex);
        byte[] msg       = hex.parseHex(frameWithoutMicHex);

        // DevAddr em little-endian (invertido)
        byte[] devAddrLe = new byte[]{ devAddrBe[3], devAddrBe[2], devAddrBe[1], devAddrBe[0] };

        // FCnt em little-endian 32-bit
        byte[] fcntLe = ByteBuffer.allocate(4)
                                  .order(ByteOrder.LITTLE_ENDIAN)
                                  .putInt(fcnt)
                                  .array();

        // Monta o bloco B0 (16 bytes)
        byte[] b0 = new byte[16];
        b0[0]  = 0x49;
        b0[1]  = 0x00; b0[2] = 0x00; b0[3] = 0x00; b0[4] = 0x00;
        b0[5]  = 0x00; // Dir = 0 (uplink)
        b0[6]  = devAddrLe[0]; b0[7]  = devAddrLe[1];
        b0[8]  = devAddrLe[2]; b0[9]  = devAddrLe[3];
        b0[10] = fcntLe[0];    b0[11] = fcntLe[1];
        b0[12] = fcntLe[2];    b0[13] = fcntLe[3];
        b0[14] = 0x00;
        b0[15] = (byte) msg.length;

        // Concatena B0 || msg
        byte[] data = new byte[b0.length + msg.length];
        System.arraycopy(b0,  0, data, 0,         b0.length);
        System.arraycopy(msg, 0, data, b0.length, msg.length);

        // AES-128-CMAC (RFC 4493) — sem dependências externas
        byte[] mic = aesCmac(nwksKey, data);
        return hex.formatHex(mic, 0, 4);
    }

    // AES-128-CMAC conforme RFC 4493
    private static byte[] aesCmac(byte[] key, byte[] data) throws Exception {
        byte[] L  = aesCbc(key, new byte[16], new byte[16]);
        byte[] K1 = generateSubkey(L);
        byte[] K2 = generateSubkey(K1);

        int blockCount = (data.length + 15) / 16;
        boolean lastBlockComplete = (data.length > 0) && (data.length % 16 == 0);
        if (blockCount == 0) { blockCount = 1; lastBlockComplete = false; }

        byte[] lastBlock = new byte[16];
        if (lastBlockComplete) {
            System.arraycopy(data, (blockCount - 1) * 16, lastBlock, 0, 16);
            xorBlock(lastBlock, K1);
        } else {
            int sz = data.length % 16;
            System.arraycopy(data, (blockCount - 1) * 16, lastBlock, 0, sz);
            lastBlock[sz] = (byte) 0x80;
            xorBlock(lastBlock, K2);
        }

        byte[] x = new byte[16];
        for (int i = 0; i < blockCount - 1; i++) {
            byte[] block = new byte[16];
            System.arraycopy(data, i * 16, block, 0, 16);
            xorBlock(block, x);
            x = aesCbc(key, new byte[16], block);
        }
        xorBlock(lastBlock, x);
        return aesCbc(key, new byte[16], lastBlock);
    }

    private static byte[] aesCbc(byte[] key, byte[] iv, byte[] data) throws Exception {
        Cipher cipher = Cipher.getInstance("AES/CBC/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE,
                    new SecretKeySpec(key, "AES"),
                    new IvParameterSpec(iv));
        return cipher.doFinal(data);
    }

    private static byte[] generateSubkey(byte[] input) {
        byte[] output = new byte[16];
        boolean msb = (input[0] & 0x80) != 0;
        for (int i = 0; i < 15; i++) {
            output[i] = (byte) ((input[i] << 1) | ((input[i + 1] & 0xFF) >> 7));
        }
        output[15] = (byte) (input[15] << 1);
        if (msb) output[15] ^= 0x87;
        return output;
    }

    private static void xorBlock(byte[] a, byte[] b) {
        for (int i = 0; i < 16; i++) a[i] ^= b[i];
    }

    public static void main(String[] args) throws Exception {
        // Exemplo — substitua pelos valores reais obtidos após o JOIN
        String mic = calcularMic(
            "151f7779d7aaee422a74568380db7045",  // NwkSKey derivada no JOIN
            "01a2b3c4",                           // DevAddr atribuído pelo ChirpStack
            1,                                    // FCnt do uplink
            "40C4B3A20100010001..."               // Frame raw sem os 4 bytes de MIC
        );
        System.out.println("MIC: " + mic);
    }
}
```

> Para compilar e executar: `javac LoRaWANMic.java && java LoRaWANMic`
> Requer Java 17+ para `HexFormat`. Em versões anteriores, substitua `HexFormat.of().parseHex(...)` por `javax.xml.bind.DatatypeConverter.parseHexBinary(...)`.

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
