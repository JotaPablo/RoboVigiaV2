# Robô Vigia Inteligente com Controle Web - Raspberry Pi Pico

Sistema de vigilância robótica com controle remoto via Wi-Fi utilizando RP2040 (Raspberry Pi Pico), que monitora um ambiente industrial, detecta de obstáculos, detecta intrusos e gerencia máquinas através de uma interface web.

## Demonstração em Vídeo
[![Assista ao vídeo no YouTube](https://img.youtube.com/vi/GTNdH3VMdhc/hqdefault.jpg)](https://youtu.be/GTNdH3VMdhc)

## Funcionalidades Principais
 
- **Controle Web em Tempo Real**  
  Move o robô com setas virtuais (▲▼◀▶) e visualiza a posição atual

- **Monitoramento Industrial**  
  Verifica o status de máquinas (ligadas/desligadas) e as controla remotamente

- **Sistema de Segurança**  
  Detecta intrusos automaticamente e permite captura remota

- **Sistema de Detecção Inteligente**
  - Algoritmo Bresenham para detecção de obstáculos em linha de visão  
  - Verificação de colisões em tempo real  
 
  
## Componentes Utilizados

| Componente                 | GPIO/Pino     | Função                                                      |
|----------------------------|---------------|-------------------------------------------------------------|
| Display OLED SSD1306       | 14 (SDA), 15 (SCL) | Exibição de mensagens do sistema     |
| Matriz de LEDs WS2812B     |  | Representação visual do mapa e elementos do ambiente       |
| LED RGB Vermelho           | 13            | Indicador de colisões                    |
| LED RGB Verde              | 11            | Indicador de operação bem sucedida
| LED RGB Azul               | 12            | Feedback de comandos recebidos                             |
| Buzzer                     | 21            | Alertas sonoros e confirmação de operações                 |personalizável                             |
| Botão Joystick             | 22            | Entrada em modo BOOTSEL para atualizações                  |


## Estrutura do Código

- **`main.c`** - Núcleo do sistema:
  - Inicialização de hardware  
  - Lógica principal de controle  

- **Subsistemas Críticos**  
  - `atualiza_leds()` - Renderização do mapa na matriz LED  
  - `tem_obstaculo_entre()` - Detecção de obstáculos entre dois objetos 
  - `liga_maquina()` - Liga uma maquina e marca um tempo para desliga-la 
  - `captura_intruso()` - Verifica e remove intrusos nas adjacências
  - `move_robo()` - Movimentação com verificação de colisões

- **Serviços Web**  
  - `tcp_server_recv()` - Manipulação de requisições HTTP  
  - `user_requests` - Responde as requisões dos usuários 
  - Interface web responsiva com atualização em tempo real  

- **Biblioteca**  
  - `ssd1306`/`neopixel`/`buzzer` - Controle de periféricos

## Endpoints de Controle

O robô responde a estes comandos via HTTP GET:

| URL            | Ação                                | Parâmetros           |
|----------------|-------------------------------------|---------------------|
| `/up`          | Move o robô para cima               | -                   |
| `/down`        | Move o robô para baixo              | -                   |
| `/left`        | Move o robô para esquerda           | -                   |
| `/right`       | Move o robô para direita            | -                   |
| `/capturar`    | Tenta capturar intruso adjacente    | -                   |
| `/ligarmaquina`| Liga máquina próxima por 10 segundos| -                   |

**Exemplo de uso:**  
`http://IP_DO_ROBO/up` - Movimenta o robô para cima  
`http://IP_DO_ROBO/capturar` - Ativa o mecanismo de captura

## ⚙️ Instalação e Uso

1. **Pré-requisitos**
   - Clonar o repositório:
     ```bash
     git clone https://github.com/JotaPablo/RoboVigia.git
     cd RoboVigia
     ```
   - Instalar o **Visual Studio Code** com as extensões:
     - **C/C++**
     - **Pico SDK Helper** ou extensão da Raspberry Pi Pico
     - **Compilador ARM GCC**
     - **CMake Tools**

2. **Configurar Wi-Fi**
   - Abra o arquivo `main.c` e atualize as credenciais:
     ```c
     #define WIFI_SSID "SEU_SSID"
     #define WIFI_PASSWORD "SUA_SENHA"
     ```
     → Substitua pelos dados da sua rede Wi-Fi

3. **Compilação**
   - Compile o projeto manualmente via terminal:
     ```bash
     mkdir build
     cd build
     cmake ..
     make
     ```
   - Ou utilize a opção **Build** da extensão da Raspberry Pi Pico no VS Code.

4. **Execução**
   - Conecte o Raspberry Pi Pico no modo BOOTSEL
   - Copie o arquivo `.uf2` para o dispositivo `RPI-RP2`