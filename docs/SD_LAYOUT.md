# Estrutura do microSD

```text
/JOGOS/GBC/
├── jogos/           # ROMs `.gb` e `.gbc`
├── cover/           # capas .cov (prioridade)
├── png/             # diretório alternativo para .cov
├── capas_originais/ # imagens de origem, para organização
├── cache_indices/
├── relatorios/
└── save/            # estados/metadados próprios do sistema
```

O nome-base deve coincidir: `Meu Jogo.gbc` corresponde a `Meu Jogo.cov`. O diretório `png/` não habilita decodificação PNG no firmware.

NS3C v1:104×136, RGB565little-endian,12bytes de cabeçalho, reservado0, total28300bytes. Conversão no computador com Python3/Pillow:

```text
python -m pip install Pillow
python tools/convert_gbc_covers.py pasta-das-imagens pasta-de-saida --preview
```

O conversor preserva proporções, EXIF, transparência e perfil ICC→sRGB, sem aumentar contraste ou saturação. Para abrir uma capa .cov existente como PNG sem filtros, apenas Python3:

```text
python tools/preview_cov.py "Meu Jogo.cov" "preview.png"
```

Estados incluem identidade do caminho/ROM e CRC. Não renomeie ou misture arquivos de save entre sistemas esperando compatibilidade. A SRAM convencional .sav fica desativada por padrão; save states manuais continuam incluindo RAM de cartucho. Falhas de escrita preservam os arquivos anteriores sempre que a recuperação puder ser confirmada.
