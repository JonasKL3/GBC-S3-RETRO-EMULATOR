## GBC-S3 DEV 0.3.8 — sem telemetria contínua

Publicação inicial do emulador independente de Game Boy / Game Boy Color para ESP32-S3 N16R8, seguindo a estrutura dos projetos GB-S3 e NES-S3.

### Incluído

- Núcleo GNUBoy, Dabble e módulos do sketch incorporados.
- Catálogo com busca/favoritos/recentes, capas NS3C com cache, áudio MAX98357A e pausa com estados próprios.
- Vídeo assíncrono e remoção dos relatórios contínuos de FPS/tempos/pilha/áudio.
- Fontes, ferramentas, estrutura do SD, testes e avisos de licença.

### Instalar

Extraia o ZIP completo e abra `firmware/GBC_S3/GBC_S3.ino`. Use seu User_Setup.h atual. ROMs `.gb` e `.gbc` em `/JOGOS/GBC/jogos/`, capas em `/JOGOS/GBC/cover/`. Perfil e controles no README.

### Validação e limites

Compilação ESP32-S3 e testes de host aprovados, com registros em `audit/`. Não houve nova medição física desta versão sem telemetria; não é uma release estável certificada. Autosave e SRAM convencional iniciamOFF. Falhas intermitentes de SD não foram declaradas resolvidas.

O GNUBoy mantém seus avisos GPL-2.0-ou-posterior. DabbleESP32 e bibliotecas externas têm termos próprios; os avisos aplicáveis estão preservados nos fontes e em `licenses/`.

Não contém ROMs comerciais ou BIOS. O ZIP é um pacote de fontes, não um binário pronto para gravar. Confira `SHA256SUMS.txt` dos assets para integridade.
