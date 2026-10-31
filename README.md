# GBC-S3 RETRO — Emulador Game Boy / Game Boy Color para ESP32-S3

**Emulador offline para ESP32-S3 N16R8 com ILI9341, microSD, gamepad BLE Dabble e áudio I2S MAX98357A.**

> **Release estável 0.3.8.** Publicação do projeto independente com fontes completos, build Arduino-ESP32 e testes no computador aprovados. A medição contínua de desempenho foi removida a pedido do autor. Versão testada pelo autor na placa e publicada como release estável após sua confirmação.

## Recursos

- Núcleo **GNUBoy** incorporado ao sketch; ROMs `.gb` e `.gbc` carregadas em PSRAM antes da emulação.
- Catálogo de duas colunas: jogos à esquerda, capa à direita; até **500 ROMs**, busca, favoritos, recentes e continuar último jogo.
- Capas NS3C v1 **104 × 136 / RGB565 little-endian**, cache de oito imagens e leitura incremental para não bloquear a navegação.
- Núcleo GNUBoy GB/CGB com framebuffer RGB565 e saída PCM; escala e envio executados fora do loop da emulação.
- Modos AMPLIADO e NATIVA, vídeo assíncrono, atualização de regiões alteradas e proteção dos buffers durante o envio.
- **START+SELECT por 700 ms** abre a pausa: continuar, salvar, carregar, slot, autosave, reiniciar, configurações e sair.
- Estados próprios nos slots **0–3 e 9**, com identidade da ROM, CRC, temporário, backup e recuperação conservadora.
- Áudio mono I2S para MAX98357A, com volume e opção de desligar pela pausa.
- **Sem relatórios contínuos de FPS, tempos, pilha ou contadores de áudio/vídeo.** Temporização, watchdog, sincronização e avisos de erro continuam ativos.

O autosave periódico e a persistência convencional `.sav` iniciam **desligados**. Saves manuais continuam disponíveis e incluem a RAM de cartucho. Cada sistema mantém sua própria pasta no SD.

## Hardware de referência

| Sinal | GPIO |
|---|---:|
| HSPI SCK / MOSI / MISO | 12 / 11 / 13 |
| ILI9341 CS / DC / RST | 10 / 9 / 4 |
| microSD CS | 14 |
| MAX98357A BCLK / LRC / DIN | 15 / 16 / 17 |

ESP32-S3 N16R8, flash **16 MB**, OPI PSRAM **8 MB**, CPU **240 MHz**, TFT ILI9341 **320 × 240**. Na TFT_eSPI, habilite `USE_HSPI_PORT` e desative `TOUCH_CS 14`. O SD usa o mesmo SPIClass do TFT, com inicialização SD-FIRST a 1 MHz.

## Instalação rápida

1. Baixe o ZIP em [Releases](https://github.com/JonasKL3/GBC-S3-RETRO-EMULATOR/releases) e extraia em uma pasta nova.
2. Instale **Arduino-ESP32 3.3.12** e **TFT_eSPI 2.5.43**, versões usadas no build desta publicação. GNUBoy e Dabble já estão em `src/`.
3. Mantenha o `User_Setup.h` que funciona com seus outros emuladores. A cópia em `config/` é referência de pinagem.
4. Abra [`firmware/GBC_S3/GBC_S3.ino`](firmware/GBC_S3/GBC_S3.ino). Selecione ESP32S3 Dev Module, 240 MHz, flash 16 MB, OPI PSRAM, Huge APP, **LoopCore=0 / EventsCore=1**.
5. Copie ROMs `.gb` e `.gbc` para **`/JOGOS/GBC/jogos/`**. Capas `.cov` ficam em **`/JOGOS/GBC/cover/`**; saves em **`/JOGOS/GBC/save/`**.
6. Use Upload normal e Monitor Serial **115200**. Conecte o Dabble ao dispositivo **`ESP32-S3-GBC-LAB`**.

Consulte [instalação](docs/INSTALLATION.md), [controles](docs/CONTROLS.md), [estrutura do SD](docs/SD_LAYOUT.md) e [testes e limitações](docs/TESTS_AND_LIMITATIONS.md).

## Estrutura

```text
GBC-S3-RETRO-EMULATOR/
├── firmware/GBC_S3/  # sketch, núcleo e Dabble incorporados
├── docs/              # instalação, hardware, controles e limitações
├── tests/             # regressões do core e dos módulos reais
├── tools/             # conversor e prévia de capas
├── config/            # referência do User_Setup.h
├── sd_template/       # diretórios próprios do sistema
├── audit/             # resultados de build, testes e hashes
├── release/           # notas e empacotamento reproduzível
└── licenses/ e references/
```

## Validação

O firmware desta versão passou em **compilação completa para ESP32-S3** e testes de host com instrumentação de comportamento indefinido. A documentação pública não altera os fontes do firmware validado. Os registros estão em [`audit/`](audit/).

Os testes cobrem o núcleo real com cartuchos sintéticos, estados, vídeo com periféricos simulados, cache de capas e metadados. Não equivalem a medir FPS, brilho, rádio, áudio ou estabilidade do SD na placa. Permanecem relatos de falhas intermitentes de armazenamento com códigos 5/9; a publicação não as declara resolvidas.

## Limitações e licenças

- Save states são próprios desta implementação e não são intercambiáveis entre GB, GBC, NES ou SMS.
- Reativar autosave pode causar uma pausa de gravação no SD. O watchdog não foi desativado.
- Não inclui ROMs comerciais, BIOS ou imagens de jogos. 
- O alvo é GB/GBC; não inclui GBA. Compatibilidade de cada jogo e mapper exige ensaio.
- O GNUBoy mantém seus avisos GPL-2.0-ou-posterior. DabbleESP32 e bibliotecas externas têm termos próprios; os avisos aplicáveis estão preservados nos fontes e em `licenses/`.

Veja [créditos](CREDITS.md), [licenças](LICENSE-NOTICE.md) e [limitações](docs/TESTS_AND_LIMITATIONS.md). Projeto independente, sem afiliação com Nintendo ou com os autores dos jogos.
