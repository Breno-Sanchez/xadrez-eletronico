# Visão geral

O **Xadrez Eletrônico** é um protótipo de tabuleiro inteligente capaz de detectar automaticamente a ocupação das casas por peças de xadrez.

O projeto foi desenvolvido para a disciplina **OI25CP-7CPE** e integra eletrônica digital, sensores magnéticos, matriz multiplexada, firmware embarcado e organização de projeto técnico.

## Objetivo

Construir um tabuleiro de xadrez capaz de identificar a presença das peças nas casas usando reed switches e um ESP32.

## Princípio de operação

Cada casa possui um reed switch. Cada peça possui um ímã de neodímio na base. Ao posicionar a peça sobre uma casa, o reed switch fecha o contato. O ESP32 realiza uma varredura da matriz e identifica as casas ocupadas.

## Escopo

Incluído no projeto:

- matriz 8x8 de sensores;
- firmware para leitura da matriz;
- documentação de hardware;
- documentação de montagem;
- roteiro de testes;
- organização de BOM, imagens, esquemáticos e STL.

Fora do escopo inicial:

- validação completa de regras oficiais de xadrez;
- engine de xadrez;
- interface gráfica final;
- comunicação com servidores externos.
