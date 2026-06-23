# BOM — Bill of Materials

Lista de materiais do projeto **Xadrez Eletrônico**.

| Item | Componente | Quantidade | Especificação | Observações |
|---:|---|---:|---|---|
| 1 | ESP32 DevKit V1 | 1 | Microcontrolador ESP32 | Controle da matriz, Wi-Fi e comunicação serial |
| 2 | Reed switch | 64 | Normalmente aberto | Um sensor por casa |
| 3 | Diodo de sinal | 64 | Ex.: 1N4148 | Isolamento elétrico da matriz |
| 4 | Resistor | 8 ou mais | 10 kΩ | Pull-up/pull-down conforme estratégia de leitura |
| 5 | Ímã de neodímio | 32 | Pequeno, para base da peça | Acionamento dos reed switches |
| 6 | Peças de xadrez | 32 | Conjunto padrão | Adaptadas com ímãs |
| 7 | Jumpers | Conforme montagem | Macho-fêmea/macho-macho | Ligação dos barramentos |
| 8 | Fios | Conforme montagem | Rígidos ou flexíveis | Linhas e colunas da matriz |
| 9 | Base do tabuleiro | 1 | MDF, acrílico, papelão ou impresso | Estrutura física |
| 10 | Cabo USB | 1 | USB para ESP32 | Alimentação e gravação |
| 11 | Fita/cola/fixação | Conforme montagem | Fita, cola quente ou similar | Organização mecânica |
