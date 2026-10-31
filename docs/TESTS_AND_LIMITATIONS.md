# Testes e limitações

## Build registrado

Arduino-ESP32 3.3.12, TFT_eSPI2.5.43, ESP32-S3 N16R8, 240MHz, OPI PSRAM, Huge APP, LoopCore=0, EventsCore=1: PASS.

- Programa: 805491bytes.
- Variáveis globais: 100900bytes; alocações dinâmicas não estão integralmente neste resumo.
- ELF SHA256: `c65d8f224c177b58cbc780de43b829cc74fc4fddb9545a7c271b2f502e1779b7`.
- Strings dos relatórios periódicos ausentes no ELF. Núcleo e BLE preservados em relação à base anterior.

Os fontes do firmware nesta publicação são idênticos aos do pacote validado. A apresentação pública reorganiza documentação e avisos, sem recompilar ou alterar a lógica do jogo.

## Regressões no computador

Python3 e Zig0.14.1:

```text
python tests/run_audit_tests.py --zig CAMINHO_DO_ZIG
```

Core real e testes C/C++ com instrumentação de comportamento indefinido. Há regressões de estado, mappers/CPU, vídeo assíncrono, pixels, fila, cache de capas e metadados. Os periféricos são simulados; testes isolados do wrapper de saves usam uma API de core controlada para injetar falhas.

No GBC:seis cartuchos sintéticos, mappers/RTC/áudio/estado, equivalência de frames com pixels alternados, mailbox e atualização de faixas RGB565.

## Limitações atuais

- O autor confirmou o teste da versão sem telemetria na placa e sua publicação como release estável. Essa confirmação não representa uma medição numérica de FPS nem garante compatibilidade com todas as ROMs.
- Há erros intermitentes de armazenamento5/9 nos logs de uso. Build e testes não provam que cartão, conexão e gravação com BLE estejam resolvidos.
- Autosave periódicoOFF; reativá-lo pode causar pausa síncrona de gravação. Saves manuais continuam dependentes do SD.
- O barramento40MHz limita imagens completas. 240×216RGB565 exige no mínimo20,736ms só no fio. Velocidade emulada e atualização do LCD são grandezas diferentes.
- Compatibilidade de jogos e mappers GB/CGB depende de ensaio; não há suporte GBA.
- A redução de telemetria retira trabalho adicional, sem atribuir ganho numérico de FPS que não foi medido.

Use os logs e hashes em `audit/` para identificar exatamente a entrega testada.
