# Hardware

ESP32-S3 N16R8: flash16MB, OPI PSRAM8MB, CPU240MHz. Display ILI9341 com interface SPI, áudio MAX98357A e cartão microSD no mesmo HSPI.

| Periférico | Sinal | GPIO |
|---|---|---:|
| TFT/SD | SCK / MOSI / MISO | 12 / 11 / 13 |
| TFT | CS / DC / RST | 10 / 9 / 4 |
| SD | CS | 14 |
| I2S | BCLK / LRC / DIN | 15 / 16 / 17 |

Habilite `USE_HSPI_PORT`. Desative `TOUCH_CS 14`: esse GPIO pertence ao SD. GPIO17 pertence ao áudio; não conectar IRQ do touch nele. O código não controla backlight por PWM. A referência continua com o LED/BL ligado como no hardware original.

SD-FIRST a1MHz, TFT a40MHz na configuração de referência; o mesmo objeto SPIClass é compartilhado. Não crie um segundo controlador usando os mesmos fios. Não foi ativado DMA nesta revisão. Não foi feita mudança global na biblioteca TFT_eSPI.
