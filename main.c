#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"   
#include "hardware/timer.h"
#include "hardware/adc.h"

#include "lib/ssd1306.h"
#include "lib/neopixel.h"
#include "lib/buzzer.h"
  
#include "lwip/pbuf.h"           // Lightweight IP stack - manipulação de buffers de pacotes de rede
#include "lwip/tcp.h"            // Lightweight IP stack - fornece funções e estruturas para trabalhar com o protocolo TCP
#include "lwip/netif.h"          // Lightweight IP stack - fornece funções e estruturas para trabalhar com interfaces de rede (netif)

// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID "Tadeu"
#define WIFI_PASSWORD "mariona6"

// Definição dos Pinos
#define RED_PIN       13
#define GREEN_PIN     11
#define BLUE_PIN      12
#define BUTTON_A      5
#define BUTTON_B      6
#define BUTTON_JOYSTICK 22
#define BUZZER_PIN    21

#define CYW43_LED_PIN CYW43_WL_GPIO_LED_PIN

//Definição das variaveis do joystick
#define VRX_PIN 27  
#define VRY_PIN 26
#define ADC_MAX 4095
#define CENTRO 2047
#define DEADZONE 250  // Zona morta de 250 ao redor do centro (2047)

static volatile uint16_t vrx_valor;
static volatile uint16_t vry_valor;

// Variáveis para debounce dos botões (armazenam tempo do último acionamento)
static volatile uint32_t last_time_button_a = 0;
static volatile uint32_t last_time_button_b = 0;

// Estrutura do display OLED
static ssd1306_t ssd; 

//====================================
//      Variáveis do Programa        
//===================================

#define VAZIO      0
#define MAQUINA_1  1
#define MAQUINA_2  2
#define INTRUSO    3
#define OBSTACULO  9

#define COMBUSTIVEL_1 4 // Combustivel relativo a máquina 1
#define COMBUSTIVEL_2 5 // Combustivel relativo a máquina 2
#define COMBUSTIVEL_MAX 2 // Quantidade máxima que o máquina pode armazenar

#define MAPA_TAM 5

int mapa[5][5] = {
    {0, 0, 0, 1, 4},
    {0, 9, 0, 0, 0},
    {3, 9, 0, 0, 0},
    {0, 9, 0, 0, 0},
    {0, 0, 0, 2, 5}
};

// Coordenadas do Robo
int robo_x = 2;
int robo_y = 2;

uint combustivel_robo = 0; // 0 - Nenhum; 4 - combustivel da Maquina 1; 5 - Combustivel da Maquina 2
bool combustivel_1_disponivel = true;
bool combustivel_2_disponivel = true;
int combustivel_maq1 = COMBUSTIVEL_MAX;
int combustivel_maq2 = COMBUSTIVEL_MAX;

// Presença do intruso
bool intruso_detectado = false;

// Variáveis para controlar o piscar do LED RGB sem bloquear o programa com sleep
uint32_t led_gpio = 0;           // Pino GPIO que está conectado ao LED
uint32_t led_duracao = 0;        // Duração de cada estado (ligado/desligado) em milissegundos
uint32_t led_repeticoes = 0;     // Número de mudanças de estado restantes (liga/desliga alternado)
uint32_t contador = 0;           // Próximo momento em que o LED deve mudar de estado (em ms)


// Função para iniciar o processo de piscar o LED
void pisca_led(uint gpio, uint duracao_ms, uint repeticoes){
    //Desliga todos inicialmente
    gpio_put(RED_PIN, false);
    gpio_put(GREEN_PIN, false);
    gpio_put(BLUE_PIN, false);

    led_gpio = gpio;
    led_duracao = duracao_ms;
    led_repeticoes = (repeticoes * 2) - 1; // Cada piscar tem 2 estados(ligado e desligado); subtrai 1 pois o primeiro liga já agora
    contador = to_ms_since_boot(get_absolute_time()) + duracao_ms; // Define o próximo tempo para troca de estado

    gpio_put(led_gpio, true);
}

// Função de atualização do estado do led (Deve ser chamada frequentemente na main)
void led_update(){
    uint32_t agora = to_ms_since_boot(get_absolute_time());

    if(led_repeticoes && agora >= contador){                 // Se ainda há repetições e chegou a hora de trocar
        contador = agora + led_duracao;                      // Atualiza o próximo tempo para mudança de estado

        led_repeticoes--;                            
        if(led_repeticoes % 2 == 1) gpio_put(led_gpio, true);   // Se ímpar, liga o LED
        else gpio_put(led_gpio, false);                         // Se par, desliga o LED
    }
}

// Função para movimentar o robô na fábrica
void move_robo(int x, int y) {
    int novo_x = robo_x + x;
    int novo_y = robo_y + y;

    //Verifica se não está no limite do mapa
    if (novo_x >= 0 && novo_x < MAPA_TAM && novo_y >= 0 && novo_y < MAPA_TAM && mapa[novo_y][novo_x] == VAZIO) {
        robo_x = novo_x;
        robo_y = novo_y;
    } else {
        beep(1000, 200, 2);
        pisca_led(RED_PIN, 200, 2);
    }
}

// Função para tentar criar uma linha entre 2 pontos e detectar se há um obstáculos entre eles
bool tem_obstaculo_entre(int x1, int y1, int x2, int y2) {
     // Calcula as diferenças absolutas entre os pontos
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);

    // Define o sentido do deslocamento nos eixos X e Y
    int sx = x1 < x2 ? 1 : -1;  // Se x2 > x1, anda para direita; senão, esquerda
    int sy = y1 < y2 ? 1 : -1;  // Se y2 > y1, anda para baixo; senão, cima

    // Inicializa o erro da linha (diferença entre os eixos)
    int err = dx - dy;

    while (true) {
        // Verifica obstáculo antes de qualquer movimento
        if (mapa[y1][x1] == OBSTACULO) return true; // Retorna verdadeiro se houver um obstaculo
        
        // Se cheguei no ponto final, paro
        if (x1 == x2 && y1 == y2) break;

        // Calcula erro acumulado
        int e2 = 2 * err;
        
        // Decide se move no eixo X
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }

        // Decide se move no eixo Y
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
    
    // Não encontrou obstáculo no caminho
    return false;
}

bool atualiza_leds_flag = false; // Para sinalizar quando é preciso atualizar a matriz de leds

// Função para atualizar a matriz de leds
void atualiza_leds() {

     atualiza_leds_flag = false;

    for (int y = 0; y < MAPA_TAM; y++) {
        for (int x = 0; x < MAPA_TAM; x++) {
            uint8_t r = 0, g = 0, b = 0;
            bool visivel = true; // Assume-se que o objeto é visível inicialmente

            // Verifica se há obstáculos entre o robô e a célula atual
            if (mapa[y][x] != OBSTACULO && tem_obstaculo_entre(robo_x, robo_y, x, y)) {
                visivel = false;  // Não será visível se houver obstáculo entre
            }

            if (x == robo_x && y == robo_y) {
                // Desenha o robô, cor cinza
                r = 1; g = 1; b = 1;
            }
            else if (mapa[y][x] == OBSTACULO && visivel) {
                // Desenha o obstáculo, cor branca
                r = 10; g = 10; b = 10;
            }
            else if (mapa[y][x] == MAQUINA_1 && visivel) {
                // Desenha a máquina 2, cor laranja se combustivel for 2, amarelo se for 1, e amarelo apagado se for 0                
                if (combustivel_maq1 == COMBUSTIVEL_MAX) { 
                    r = 13 ; g = 2; b = 0; // Laranja
                } else if (combustivel_maq1 == 1) { 
                    r = 20 ; g = 20; b = 0; // Amarelo
                } else {
                    r = 1; g = 1; b = 0; // Amarelo apagado
                }
            }
            else if (mapa[y][x] == MAQUINA_2 && visivel) {
                // Desenha a máquina 2, cor laranja se combustivel for 2, amarelo se for 1, e amarelo apagado se for 0
                if (combustivel_maq2 == COMBUSTIVEL_MAX) { 
                    r = 13 ; g = 2; b = 0; // Laranja
                } else if (combustivel_maq2 == 1) { 
                    r = 20 ; g = 20; b = 0; // Amarelo
                } else {
                    r = 1; g = 1; b = 0; // Amarelo apagado
                }
            }

            else if (mapa[y][x] == COMBUSTIVEL_1 && visivel) {
                // Desenha a carga do combustivel 1, cor violeta (ligada ou desligada)
                if (combustivel_1_disponivel) {
                    r = 20; g = 0; b = 20; // Mais clara quando ligada
                } else {
                    r = 1; g = 0; b = 1; // Mais escura quando desligada
                }
            }

            else if (mapa[y][x] == COMBUSTIVEL_2 && visivel) {
                // Desenha a carga do combustivel 2, cor violeta (ligada ou desligada)
                if (combustivel_2_disponivel) {
                    r = 20; g = 0; b = 20; // Mais clara quando ligada
                } else {
                    r = 1; g = 0; b = 1; // Mais escura quando desligada
                }
            }
            else if (mapa[y][x] == INTRUSO && visivel) {
                // Desenha o intruso, cor vermelha
                r = 20; g = 0; b = 0;
                intruso_detectado = true;
            }

            // Atualiza o LED na posição x, y com a cor calculada
            int y_invertido = (MAPA_TAM - 1) - y;
            int index = npGetIndex(x, y_invertido);
            npSetLED(index, r, g, b);
        }
    }
    npWrite();
}

// Função de callback para diminuir o combustivel das maquinas
bool consome_combustivel(struct repeating_timer *t){

    if(combustivel_maq1 > 0) combustivel_maq1 -= 1;
    if(combustivel_maq2 > 0) combustivel_maq2 -= 1;

    atualiza_leds_flag = true;
    
}

//Alarmes para recarregar um combustivel especifico
int64_t recarrega_combustivel_1(alarm_id_t id, void *user_data) {
    
    combustivel_1_disponivel = true;
    printf("Combustivel 1 foi recarregado\n");
    atualiza_leds_flag = true;
    
    return 0;  // Não repetirá o alarme
}

int64_t recarrega_combustivel_2(alarm_id_t id, void *user_data) {
    
    combustivel_2_disponivel = true;
    printf("Combustivel 2 foi recarregado\n");
    atualiza_leds_flag = true;
    
    return 0;  // Não repetirá o alarme
}

// Função para entregar e coletar o combustivel por um dos lados(cima, baixo, esquerda ou direita) e configurar o alarme para desligamento
void entrega_combustivel(int x, int y) {
    // Define as 4 posições adjacentes ao robô (frente, atrás, esquerda, direita)
    int adjacentes[4][2] = {
        {x, y - 1},  // Frente (acima)
        {x, y + 1},  // Atrás (abaixo)
        {x - 1, y},  // Esquerda
        {x + 1, y}   // Direita
    };

    // Verifica cada uma das 4 posições adjacentes
    for (int i = 0; i < 4; i++) {
        int adj_x = adjacentes[i][0];  // Coordenada X da posição adjacente
        int adj_y = adjacentes[i][1];  // Coordenada Y da posição adjacente

        // Primeiro verifica se a posição adjacente está dentro dos limites do mapa
        if (adj_x >= 0 && adj_x < MAPA_TAM && adj_y >= 0 && adj_y < MAPA_TAM) {
            
            // Caso 1: Verifica se há uma Máquina 1 na posição adjacente
            if (mapa[adj_y][adj_x] == MAQUINA_1) {
                
                // Se a máquina já está cheia ou o robô não tem o combustível correto
                if (combustivel_maq1 >= COMBUSTIVEL_MAX || combustivel_robo != COMBUSTIVEL_1) {
                    printf("Combustivel da Maquina 1 cheio ou combustivel inválido\n");
                    beep(1000, 200, 2);            // Feedback sonoro de erro
                    pisca_led(RED_PIN, 200, 2);     // Feedback visual de erro
                }
                // Caso contrário, realiza a entrega do combustível
                else {
                    printf("Combustivel inserido na Maquina 1.\n");
                    combustivel_maq1 += 1;         // Incrementa o combustível da máquina
                    combustivel_robo = 0;           // Esvazia o combustível do robô
                    atualiza_leds_flag = true;      // Sinaliza para atualizar a matriz de LEDs
                    beep(2000, 200, 3);            // Feedback sonoro de sucesso
                    pisca_led(GREEN_PIN, 200, 3);  // Feedback visual de sucesso
                }
            }
            
            // Caso 2: Verifica se há uma Máquina 2 na posição adjacente
            else if (mapa[adj_y][adj_x] == MAQUINA_2) {
                
                // Se a máquina já está cheia ou o robô não tem o combustível correto
                if (combustivel_maq2 >= COMBUSTIVEL_MAX || combustivel_robo != COMBUSTIVEL_2) {
                    printf("Combustivel da Maquina 2 cheio.\n");
                    beep(1000, 200, 2);            // Feedback sonoro de erro
                    pisca_led(RED_PIN, 200, 2);    // Feedback visual de erro
                }
                // Caso contrário, realiza a entrega do combustível
                else {
                    printf("Combustivel inserido na Maquina 2.\n");
                    combustivel_maq2 += 1;         // Incrementa o combustível da máquina
                    combustivel_robo = 0;           // Esvazia o combustível do robô
                    atualiza_leds_flag = true;      // Sinaliza para atualizar a matriz de LEDs
                    beep(2000, 200, 3);            // Feedback sonoro de sucesso
                    pisca_led(GREEN_PIN, 200, 3);  // Feedback visual de sucesso
                }
            }
        }
    }
}

void coleta_combustivel(int x, int y) {
    // Define as 4 posições adjacentes ao robô (frente, atrás, esquerda, direita)
    int adjacentes[4][2] = {
        {x, y - 1},  // Frente (acima)
        {x, y + 1},  // Atrás (abaixo)
        {x - 1, y},  // Esquerda
        {x + 1, y}   // Direita
    };

    // Verifica cada uma das 4 posições adjacentes
    for (int i = 0; i < 4; i++) {
        int adj_x = adjacentes[i][0];  // Coordenada X da posição adjacente
        int adj_y = adjacentes[i][1];  // Coordenada Y da posição adjacente

        // Primeiro verifica se a posição adjacente está dentro dos limites do mapa
        if (adj_x >= 0 && adj_x < MAPA_TAM && adj_y >= 0 && adj_y < MAPA_TAM) {
            
            // Verifica se há um depósito de combustível (tipo 1 ou 2) na posição
            if (mapa[adj_y][adj_x] == COMBUSTIVEL_1 || mapa[adj_y][adj_x] == COMBUSTIVEL_2) {
                
                // Se o robô já está carregando combustível (não pode coletar outro)
                if (combustivel_robo != 0) {
                    printf("Robô já possui combustivel\n");
                    beep(1000, 200, 2);         // Feedback sonoro de erro
                    pisca_led(RED_PIN, 200, 2); // Feedback visual de erro
                }
                // Se o robô está vazio e pode coletar
                else {
                    // Caso 1: Combustível tipo 1 disponível
                    if (mapa[adj_y][adj_x] == COMBUSTIVEL_1 && combustivel_1_disponivel) {
                        combustivel_1_disponivel = false;   // Marca como coletado
                        combustivel_robo = COMBUSTIVEL_1;    // Carrega no robô
                        
                        // Programa o respawn após 3 segundos
                        add_alarm_in_ms(3000, recarrega_combustivel_1, NULL, false);
                    }
                    // Caso 2: Combustível tipo 2 disponível
                    else if (mapa[adj_y][adj_x] == COMBUSTIVEL_2 && combustivel_2_disponivel) {
                        combustivel_2_disponivel = false;   // Marca como coletado
                        combustivel_robo = COMBUSTIVEL_2;    // Carrega no robô
                        
                        // Programa o respawn após 3 segundos
                        add_alarm_in_ms(3000, recarrega_combustivel_2, NULL, false);
                    }

                    // Feedback de sucesso
                    printf("Robô coletou combustível\n");
                    atualiza_leds_flag = true;     // Sinaliza para atualizar LEDs
                    beep(2000, 200, 3);          // Feedback sonoro de sucesso
                    pisca_led(GREEN_PIN, 200, 3); // Feedback visual de sucesso
                }
            }
        }
    }
}

void captura_intruso(int x, int y){
    // Coordenadas adjacentes (frente, atrás, esquerda, direita)
    int adjacentes[4][2] = {
        {x, y - 1},  // Frente (acima)
        {x, y + 1},  // Atrás (abaixo)
        {x - 1, y},  // Esquerda
        {x + 1, y}   // Direita
    };

    // Verifica cada posição adjacente
    for (int i = 0; i < 4; i++) {
        int adj_x = adjacentes[i][0];
        int adj_y = adjacentes[i][1];

        // Verifica se a posição adjacente está dentro dos limites do mapa
        if (adj_x >= 0 && adj_x < MAPA_TAM && adj_y >= 0 && adj_y < MAPA_TAM) {
            if(mapa[adj_y][adj_x] == INTRUSO){
                mapa[adj_y][adj_x] = VAZIO;
                intruso_detectado = false;
                beep(2000, 200, 3);
                pisca_led(GREEN_PIN, 200, 3);
                atualiza_leds_flag = true;
            }
        }
    }
}

//====================================
//      Funções do Web Server        
//====================================

char html[4096]; // Cria a resposta HTML

// Função para gerir as requisições
void user_request(char *request) {
    if (strstr(request, "GET /up") != NULL) {
        move_robo(0, -1);
    } else if (strstr(request, "GET /down") != NULL) {
        move_robo(0, 1);
    } else if (strstr(request, "GET /left") != NULL) {
        move_robo(-1, 0);
    } else if (strstr(request, "GET /right") != NULL) {
        move_robo(1, 0);
    } else if (strstr(request, "GET /capturar") != NULL) {
        captura_intruso(robo_x, robo_y);
    } else if (strstr(request, "GET /entrega") != NULL) {
        entrega_combustivel(robo_x, robo_y);
    } else if (strstr(request, "GET /coleta") != NULL) {
        coleta_combustivel(robo_x, robo_y);
    }

    atualiza_leds();
}
// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p){
        tcp_close(tpcb);
        return ERR_OK;
    }

    // Alocação do request na memória dinámica
    char *request = (char *)p->payload;

    printf("Request: %s\n", request);

    // Tratamento de request - Controle dos LEDs
    user_request(request);
    
    // Instruções html do webserver
    snprintf(html, sizeof(html),
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-cache, no-store, must-revalidate\r\n"
    "Pragma: no-cache\r\n"
    "Expires: 0\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='UTF-8'>"
    "<style>"
    "body{font-family:sans-serif;text-align:center;background:#eee;color:#000;margin:10px}"
    ".info{padding:10px;margin:5px;background:#fff;border-radius:5px}"
    "button{border:0;border-radius:8px;padding:12px;margin:4px;font-size:1.1em}"
    ".ctrl{width:20vw;height:20vw;max-width:100px;max-height:100px;background:#ddd;color:#000}"
    ".status{color:%s}"
    ".btn-vermelho{background:#f44336;color:white}"
    ".btn-amarelo{background:#ffeb3b;color:black}"
    ".btn-verde{background:#4CAF50;color:white}"      // Novo estilo para botão verde
    ".btn-azul{background:#2196F3;color:white}"       // Novo estilo para botão azul
    "</style></head>"
    "<script>"
    "setInterval(function(){location.href='/';},3000);" // Scrpit para atualizar a página a cada 3 segundos
    "</script>"
    "<body><h1>ROBÔ VIGIA</h1>"

    "<div class='info'>"
    "Maquina 1: <strong id='estado-maquina1'>%d/2</strong><br>"
    "Maquina 2: <strong id='estado-maquina2'>%d/2</strong><br>"
    "</div>"

    "<div class='info'>"
    "INTRUSO: <strong id='estado-intruso' class='status'>%s</strong>"
    "</div>"

    "<div class='info'>POSIÇÃO: (%d, %d)</div>"

    // Nova seção para mostrar o combustível atual
    "<div class='info'>"
    "COMBUSTÍVEL: <strong>%s</strong>"
    "</div>"

    "<div style='margin:20px 0'>"
    "<div><a href='/up'><button class='ctrl'>▲</button></a></div>"
    "<div>"
    "<a href='/left'><button class='ctrl'>◀</button></a>"
    "<a href='/right'><button class='ctrl'>▶</button></a>"
    "</div>"
    "<div><a href='/down'><button class='ctrl'>▼</button></a></div>"
    "</div>"

    "<div style='margin-top:20px'>"
    "<a href='/capturar'><button class='btn-vermelho'>Capturar Intruso</button></a>"
    "</div>"

    // Novos botões para combustível
    "<div style='margin-top:20px'>"
    "<a href='/entrega'><button class='btn-verde'>Entregar Combustível</button></a>"
    "<a href='/coleta'><button class='btn-amarelo'>Coletar Combustível</button></a>"
    "</div>"

    "</body></html>",

    // Argumentos para os placeholders
    intruso_detectado ? "red" : "green",
    combustivel_maq1,
    combustivel_maq2,
    intruso_detectado ? "DETECTADO" : "NENHUM",
    robo_x, robo_y,
    // Novo argumento para status do combustível
    (combustivel_robo == 4) ? "Tipo 1" : 
    (combustivel_robo == 5) ? "Tipo 2" : "Nenhum"
);


    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    pbuf_free(p);
    return ERR_OK;
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Função para inicializar o servidor TCP
int server_init(void) {
    //Inicializa a arquitetura do cyw43
    while (cyw43_arch_init()){
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    // GPIO do CI CYW43 em nível baixo
    cyw43_arch_gpio_put(CYW43_LED_PIN, 0);

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 120000)){
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");

    // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default){
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server){
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    //vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK){
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");
    return 1;
}

//====================================
//      Funções de Harwdware       
//====================================

// Tratador central de interrupções de botões
static void gpio_button_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if (gpio == BUTTON_A) {
        if (current_time - last_time_button_a > 200000) {
            last_time_button_a = current_time;
            printf("Botão A pressionado\n");
        }
    } else if (gpio == BUTTON_B) {
        if (current_time - last_time_button_b > 200000) {
            last_time_button_b = current_time;
            printf("Botão B pressionado\n");
        }
    } else if (gpio == BUTTON_JOYSTICK) {
        printf("\nHABILITANDO O MODO GRAVAÇÃO\n");

        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "  HABILITANDO", 5, 25);
        ssd1306_draw_string(&ssd, " MODO GRAVACAO", 5, 38);
        ssd1306_send_data(&ssd);

        reset_usb_boot(0, 0);
    }
}

//Configuração inicial de hardware
int setup() {
    stdio_init_all();

    adc_init();
    adc_gpio_init(VRY_PIN);  // Eixo Y
    adc_gpio_init(VRX_PIN);  // Eixo X

    npInit(LED_PIN);

    display_init(&ssd);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, " Inicializando", 0, 28);
    ssd1306_send_data(&ssd);

    buzzer_init(BUZZER_PIN);

    // Configuração dos LEDs
    gpio_init(RED_PIN);
    gpio_set_dir(RED_PIN, GPIO_OUT);
    gpio_init(GREEN_PIN);
    gpio_set_dir(GREEN_PIN, GPIO_OUT);
    gpio_init(BLUE_PIN);
    gpio_set_dir(BLUE_PIN, GPIO_OUT);

    //Configuração dos Botões
    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_pull_up(BUTTON_A);

    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);

    gpio_init(BUTTON_JOYSTICK);
    gpio_set_dir(BUTTON_JOYSTICK, GPIO_IN);
    gpio_pull_up(BUTTON_JOYSTICK);

    // Configura interrupções para borda de descida (botão pressionado)
    gpio_set_irq_enabled_with_callback(BUTTON_A, GPIO_IRQ_EDGE_FALL, true, &gpio_button_handler);
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_button_handler);
    gpio_set_irq_enabled_with_callback(BUTTON_JOYSTICK, GPIO_IRQ_EDGE_FALL, true, &gpio_button_handler);

    //Inicia o servidor
    int resposta = server_init();

    ssd1306_fill(&ssd, false);
    if(resposta == -1) ssd1306_draw_string(&ssd, "Erro na Conexao", 0, 28);
    else               ssd1306_draw_string(&ssd, " Servidor Ativo ", 0, 28);
    ssd1306_send_data(&ssd);

    return resposta;
}

int main()
{
    int resposta = setup();

    if(resposta == -1) return resposta;
    
    atualiza_leds();

    struct repeating_timer timer;

    // Configura para chamar a função de consumir combustivel a cada 9 segundos
    add_repeating_timer_ms(9000, consome_combustivel, NULL, &timer);

    while (true) {
        if(atualiza_leds_flag) atualiza_leds();
        
        buzzer_update();
        led_update();
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo
        sleep_ms(200);    
    }

    cyw43_arch_deinit(); // Desativa o CYW43
    return 0;
}
