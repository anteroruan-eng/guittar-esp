#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <FastLED.h>

// Configurações da Fita LED
#define LED_PIN       13
#define NUM_LEDS      100 // 100 grupos de 3 leds
#define BRIGHTNESS    255
#define LED_TYPE      WS2811
#define COLOR_ORDER   GRB

// Configurações das Pistas
#define NUM_PISTAS     5
#define LEDS_POR_PISTA 20

// Configurações dos Botões
#define BTN_START     4
#define BTN_RED       5
#define BTN_GREEN     6
#define BTN_BLUE      7

// Configurações dos pinos para futura expansão (Pistas 4 e 5)
// #define BTN_YELLOW  16
// #define BTN_ORANGE  17

// Configuração do Buzzer
#define BUZZER_PIN    15

// Configurações do LCD I2C (Endereço comum: 0x27)
#define LCD_SDA       8
#define LCD_SCL       9
LiquidCrystal_I2C lcd(0x27, 16, 2);

CRGB leds[NUM_LEDS];

// Máquina de Estados do Jogo
enum EstadoJogo {
  ESTADO_MENU,
  ESTADO_JOGANDO,
  ESTADO_PAUSADO,
  ESTADO_GAME_OVER
};
EstadoJogo estadoAtual = ESTADO_MENU;

// Variáveis do Jogo
int pontuacao = 0;
int vidas = 5;
unsigned long ultimoMovimento = 0;
int velocidade = 140; // Tempo em milissegundos para a nota descer 1 bloco (inicia em 140ms)
int chanceDeNota = 12; // Probabilidade de gerar uma nota nova por pista (em %)

// Estados anteriores dos botões para detecção de clique (borda de descida)
bool antStart = HIGH;
bool antRed = HIGH;
bool antGreen = HIGH;
bool antBlue = HIGH;

// Protótipos das funções para o compilador
void emitirSom(unsigned int frequencia, unsigned long duracao);
void somInicio();
void somGameOver();
int obterIndiceLED(int pista, int passo);
void moverNotas();
void verificarJogada();
void acertouNota(int pista);
void errouNota(int pista);
void perdeuNota();
void verificarBotaoStart();
void mostrarMenuInicial();
void atualizarPlacar();
void atualizarVelocidade();
int obterLevel();
void desenharCenario();

void setup() {
  // Inicializa a comunicação Serial nativa USB do ESP32-S3
  Serial.begin(115200);
  
  // Inicializa I2C nos pinos corretos do ESP32-S3
  Wire.begin(LCD_SDA, LCD_SCL);
  
  // Inicializa o LCD
  lcd.init();
  lcd.backlight();
  
  // Inicializa o Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Inicializa os LEDs
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  // Inicializa Botões com Pull-up interno (pressionado = LOW)
  pinMode(BTN_START, INPUT_PULLUP);
  pinMode(BTN_RED, INPUT_PULLUP);
  pinMode(BTN_GREEN, INPUT_PULLUP);
  pinMode(BTN_BLUE, INPUT_PULLUP);

  mostrarMenuInicial();
}

void loop() {
  verificarBotaoStart();

  if (estadoAtual == ESTADO_JOGANDO) {
    // 1. Mecânica de descida das notas por tempo (sem travar o código)
    if (millis() - ultimoMovimento >= velocidade) {
      ultimoMovimento = millis();
      moverNotas();
      
      // Se após o movimento o jogo terminou por perda de vidas, interrompe o loop
      if (estadoAtual == ESTADO_GAME_OVER) {
        return;
      }
    }

    // 2. Verifica se o jogador acertou no tempo correto
    verificarJogada();
  }
}

// Emite um tom sonoro usando a função nativa do core Arduino para ESP32 (não-bloqueante)
void emitirSom(unsigned int frequencia, unsigned long duracao) {
  tone(BUZZER_PIN, frequencia, duracao);
}

// Melodia festiva de início (bloqueante, executada apenas antes do jogo começar)
void somInicio() {
  tone(BUZZER_PIN, 440, 100); // Lá
  delay(120);
  tone(BUZZER_PIN, 554, 100); // Dó#
  delay(120);
  tone(BUZZER_PIN, 659, 100); // Mi
  delay(120);
  tone(BUZZER_PIN, 880, 250); // Lá
  delay(250);
}

// Melodia triste de Game Over (bloqueante, executada após perder o jogo)
void somGameOver() {
  tone(BUZZER_PIN, 330, 150); // Mi
  delay(200);
  tone(BUZZER_PIN, 262, 150); // Dó
  delay(200);
  tone(BUZZER_PIN, 220, 150); // Lá
  delay(200);
  tone(BUZZER_PIN, 165, 400); // Mi
  delay(400);
}

// Retorna o índice físico do LED com base na pista (0 a 4) e no passo lógico (0 a 19)
// 0 é o topo (onde a nota surge) e 19 é o final (zona de impacto / Hit Zone)
int obterIndiceLED(int pista, int passo) {
  // Padrão: Pistas paralelas com a mesma orientação física
  return (pista * LEDS_POR_PISTA) + passo;
  
  // OPCIONAL: Se a sua fita foi montada em zigue-zague (serpentina)
  // Descomente as linhas abaixo e comente o "return" acima:
  /*
  if (pista % 2 == 1) {
    return (pista * LEDS_POR_PISTA) + (19 - passo);
  } else {
    return (pista * LEDS_POR_PISTA) + passo;
  }
  */
}

// Desenha a linha de mira visual fraca no LED 17 para orientar o jogador
void desenharCenario() {
  for (int pista = 0; pista < 3; pista++) {
    int idxMira = obterIndiceLED(pista, 17);
    if (leds[idxMira] == CRGB::Black) {
      leds[idxMira] = CRGB(15, 15, 15); // Linha cinza fraca
    }
  }
}

// Move as notas nas pistas ativas
void moverNotas() {
  // 1. Detectar notas que chegaram ao fim da pista (passo 19) e passaram sem clique (miss)
  for (int pista = 0; pista < 3; pista++) {
    int idxFim = obterIndiceLED(pista, 19);
    CRGB corNota = leds[idxFim];
    if ((pista == 0 && corNota == CRGB::Red) ||
        (pista == 1 && corNota == CRGB::Green) ||
        (pista == 2 && corNota == CRGB::Blue)) {
      perdeuNota();
    }
  }

  // Se o jogo acabou por falta de vidas na verificação anterior, interrompe
  if (estadoAtual == ESTADO_GAME_OVER) return;

  // 2. Desloca as notas um passo para a frente (de 19 decrescendo até 1)
  for (int pista = 0; pista < NUM_PISTAS; pista++) {
    for (int passo = 19; passo > 0; passo--) {
      int idxAtual = obterIndiceLED(pista, passo);
      int idxAnterior = obterIndiceLED(pista, passo - 1);
      leds[idxAtual] = leds[idxAnterior];
    }
    
    // 3. Gera nota no início da pista (passo 0) aleatoriamente
    int idxInicio = obterIndiceLED(pista, 0);
    // Ativa geração de notas apenas nas 3 primeiras pistas por enquanto
    if (pista < 3 && random(0, 100) < chanceDeNota) {
      if (pista == 0) leds[idxInicio] = CRGB::Red;
      else if (pista == 1) leds[idxInicio] = CRGB::Green;
      else if (pista == 2) leds[idxInicio] = CRGB::Blue;
    } else {
      leds[idxInicio] = CRGB::Black;
    }
  }

  // Redesenha a mira no cenario
  desenharCenario();
  FastLED.show();
}

// Verifica cliques nos botões de cor e valida contra a Hit Zone (passos 18 e 19)
void verificarJogada() {
  bool lerRed = digitalRead(BTN_RED);
  bool lerGreen = digitalRead(BTN_GREEN);
  bool lerBlue = digitalRead(BTN_BLUE);

  // Variáveis para indicar clique (físico ou serial)
  bool clickRed = (lerRed == LOW && antRed == HIGH);
  bool clickGreen = (lerGreen == LOW && antGreen == HIGH);
  bool clickBlue = (lerBlue == LOW && antBlue == HIGH);

  // Verifica se há comandos chegando pela Ponte Serial
  while (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'R') {
      clickRed = true;
    } else if (cmd == 'G') {
      clickGreen = true;
    } else if (cmd == 'B') {
      clickBlue = true;
    }
  }

  // Botão Vermelho (Pista 0)
  if (clickRed) {
    int idx19 = obterIndiceLED(0, 19);
    int idx18 = obterIndiceLED(0, 18);
    if (leds[idx19] == CRGB::Red || leds[idx18] == CRGB::Red) {
      acertouNota(0);
    } else {
      errouNota(0);
    }
  }

  // Botão Verde (Pista 1)
  if (clickGreen) {
    int idx19 = obterIndiceLED(1, 19);
    int idx18 = obterIndiceLED(1, 18);
    if (leds[idx19] == CRGB::Green || leds[idx18] == CRGB::Green) {
      acertouNota(1);
    } else {
      errouNota(1);
    }
  }

  // Botão Azul (Pista 2)
  if (clickBlue) {
    int idx19 = obterIndiceLED(2, 19);
    int idx18 = obterIndiceLED(2, 18);
    if (leds[idx19] == CRGB::Blue || leds[idx18] == CRGB::Blue) {
      acertouNota(2);
    } else {
      errouNota(2);
    }
  }

  antRed = lerRed;
  antGreen = lerGreen;
  antBlue = lerBlue;
}

// Ações quando o jogador acerta o timing da nota
void acertouNota(int pista) {
  pontuacao += 10;
  
  // Limpa a nota acertada substituindo por um brilho branco (feedback visual)
  int idx19 = obterIndiceLED(pista, 19);
  int idx18 = obterIndiceLED(pista, 18);
  leds[idx19] = CRGB::White;
  leds[idx18] = CRGB::White;
  FastLED.show();
  
  // Logo após o show, reseta para preto para que a nota não persista
  leds[idx19] = CRGB::Black;
  leds[idx18] = CRGB::Black;
  
  emitirSom(1000, 50); // Beep agudo de sucesso
  
  atualizarVelocidade();
  atualizarPlacar();
}

// Ações quando o jogador clica sem ter nota na zona de acerto (erro)
void errouNota(int pista) {
  if (pontuacao > 5) pontuacao -= 5;
  else pontuacao = 0;
  
  if (vidas > 0) vidas--;

  // Sinalização visual de erro na pista (pisca vermelho)
  int idx19 = obterIndiceLED(pista, 19);
  int idx18 = obterIndiceLED(pista, 18);
  leds[idx19] = CRGB(100, 0, 0); 
  leds[idx18] = CRGB(100, 0, 0);
  FastLED.show();

  emitirSom(150, 150); // Som de erro grave (buzz)
  
  if (vidas <= 0) {
    estadoAtual = ESTADO_GAME_OVER;
    atualizarPlacar();
    somGameOver();
  } else {
    atualizarPlacar();
  }
}

// Ações quando uma nota passa da zona sem ser clicada
void perdeuNota() {
  if (pontuacao > 2) pontuacao -= 2;
  else pontuacao = 0;
  
  if (vidas > 0) vidas--;

  emitirSom(100, 50); // Som discreto de nota perdida

  if (vidas <= 0) {
    estadoAtual = ESTADO_GAME_OVER;
    atualizarPlacar();
    somGameOver();
  } else {
    atualizarPlacar();
  }
}

// Gerenciamento do botão Start/Stop (Branco)
void verificarBotaoStart() {
  bool lerStart = digitalRead(BTN_START);

  if (lerStart == LOW && antStart == HIGH) {
    delay(50); // Debounce
    
    if (estadoAtual == ESTADO_MENU) {
      // Começa novo jogo
      pontuacao = 0;
      vidas = 5;
      velocidade = 100;
      estadoAtual = ESTADO_JOGANDO;
      FastLED.clear();
      desenharCenario();
      FastLED.show();
      atualizarPlacar();
      somInicio();
    } 
    else if (estadoAtual == ESTADO_JOGANDO) {
      // Pausa
      estadoAtual = ESTADO_PAUSADO;
      emitirSom(400, 100);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("  JOGO PAUSADO  ");
      lcd.setCursor(0, 1);
      lcd.print("Aperte p/ Voltar");
    } 
    else if (estadoAtual == ESTADO_PAUSADO) {
      // Despausa
      estadoAtual = ESTADO_JOGANDO;
      emitirSom(600, 100);
      atualizarPlacar();
    } 
    else if (estadoAtual == ESTADO_GAME_OVER) {
      // Volta ao Menu
      estadoAtual = ESTADO_MENU;
      mostrarMenuInicial();
    }
  }
  antStart = lerStart;
}

// Exibe a tela do menu inicial
void mostrarMenuInicial() {
  FastLED.clear();
  FastLED.show();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  LED HERO S3   ");
  lcd.setCursor(0, 1);
  lcd.print("Aperte Start -> ");
}

// Atualiza o display LCD com dados do placar ou fim de jogo
void atualizarPlacar() {
  lcd.clear();
  if (estadoAtual == ESTADO_JOGANDO) {
    lcd.setCursor(0, 0);
    lcd.print("PTS:");
    lcd.print(pontuacao);
    
    lcd.setCursor(9, 0);
    lcd.print("VIDAS:");
    lcd.print(vidas);
    
    lcd.setCursor(0, 1);
    lcd.print("VELOCIDADE: Lv.");
    lcd.print(obterLevel());
  } 
  else if (estadoAtual == ESTADO_GAME_OVER) {
    lcd.setCursor(0, 0);
    lcd.print("   GAME OVER!   ");
    lcd.setCursor(0, 1);
    lcd.print("PTS:");
    lcd.print(pontuacao);
    lcd.print("  START->Rec");
  }
}

// Altera a velocidade da descida das notas com base na pontuação
void atualizarVelocidade() {
  if (pontuacao < 100) {
    velocidade = 140;
  } else if (pontuacao < 200) {
    velocidade = 120;
  } else if (pontuacao < 400) {
    velocidade = 100;
  } else if (pontuacao < 600) {
    velocidade = 80;
  } else {
    velocidade = 60;
  }
}

// Retorna o nível de dificuldade com base nos pontos obtidos
int obterLevel() {
  if (pontuacao < 100) return 1;
  if (pontuacao < 200) return 2;
  if (pontuacao < 400) return 3;
  if (pontuacao < 600) return 4;
  return 5;
}