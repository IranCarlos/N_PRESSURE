# Projeto Angiologista

Firmware para um dispositivo médico desenvolvido com base no microcontrolador STM32H723

## Configuração do Projeto

O projeto utiliza o STM32CubeMX para a configuração inicial do hardware e o CMake para o sistema de build. A estrutura é organizada da seguinte forma:

- **Microcontrolador**: STM32H7 series.
- **Framework**: STM32Cube HAL.
- **Sistema de Build**: CMake.

### Periféricos e Controladores:

- **Display**: Utiliza um display com controlador ST7789, com comunicação via SPI e transferências de dados otimizadas com DMA.
- **UI (Interface de Usuário)**: A interface gráfica é construída com a biblioteca **LVGL v9**. A portabilidade do display (`lv_port_disp.c`) e a rotina de interrupção do SPI (`HAL_SPI_TxCpltCallback`) garantem a integração.
- **Armazenamento Persistente**: As configurações do sistema são salvas em uma memória EEPROM externa, acessada via I2C. Uma camada de abstração (`eeprom_storage_io.c`) gerencia a leitura e escrita, garantindo a integridade dos dados com verificação por CRC32.
- **Encoder Rotativo**: Um encoder é utilizado para a entrada do usuário, com sua lógica de leitura implementada no `main.c`.

## Versão da LVGL

Este projeto utiliza a versão **9** da biblioteca LVGL. A inicialização e a configuração dos buffers de display são feitas de acordo com as novas APIs da v9, como pode ser visto em `Core/Src/lv_port_disp.c`.