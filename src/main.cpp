#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <FastLED.h>

// Configurações da Fita LED
#define LED_PIN       13
#define NUM_LEDS      79 // 19 + 20 + 20 + 20 = 79 leds
#define BRIGHTNESS    255
#define LED_TYPE      WS2811
#define COLOR_ORDER   BRG

// Configurações das Pistas
#define NUM_PISTAS     4

// Configurações dos Botões
#define BTN_START     4
#define BTN_GREEN     6
#define BTN_YELLOW    16
#define BTN_RED       5
#define BTN_BLUE      7

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
int velocidade = 300; // Tempo em milissegundos para a nota descer 1 bloco (inicia em 300ms)
int chanceDeNota = 5; // Probabilidade inicial de gerar uma nota nova por pista (em %)
const char* ultimoAcerto = "";

// Variáveis para comunicação Serial (Ponte)
bool clickGreenSerial = false;
bool clickYellowSerial = false;
bool clickRedSerial = false;
bool clickBlueSerial = false;
bool clickStartSerial = false;

// Variáveis e Definições da Música de Fundo (Buzzer)
const int melodiaNotas[] = {
  220, 220, 262, 294, 220, 220, 262, 330, 294,
  220, 220, 262, 294, 262, 220, 0
};
const int melodiaTempos[] = {
  150, 150, 300, 300, 150, 150, 150, 150, 300,
  150, 150, 300, 300, 300, 300, 300
};
const int NUM_NOTAS_MELODIA = sizeof(melodiaNotas) / sizeof(melodiaNotas[0]);

int notaAtualMelodia = 0;
unsigned long tempoInicioNota = 0;
unsigned long duracaoNotaAtual = 0;
unsigned long tempoFimInterrupcao = 0;
bool emInterrupcaoSom = false;

// Estados anteriores dos botões para detecção de clique (borda de descida)
bool antStart = HIGH;
bool antGreen = HIGH;
bool antYellow = HIGH;
bool antRed = HIGH;
bool antBlue = HIGH;

// Protótipos das funções para o compilador
void emitirSom(unsigned int frequencia, unsigned long duracao);
void processarMusicaDeFundo();
void lerSerial();
void somInicio();
void somGameOver();
int obterIndiceLED(int pista, int passo);
void moverNotas();
void verificarJogada();
void acertouNota(int pista, int passoAcerto);
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
  pinMode(BTN_GREEN, INPUT_PULLUP);
  pinMode(BTN_YELLOW, INPUT_PULLUP);
  pinMode(BTN_RED, INPUT_PULLUP);
  pinMode(BTN_BLUE, INPUT_PULLUP);

  mostrarMenuInicial();
}

void loop() {
  // 1. Processa comandos da Ponte Serial
  lerSerial();

  // 2. Gerenciamento do botão Start/Stop
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

    // 3. Processa a música de fundo continuamente
    processarMusicaDeFundo();
  } else {
    // Se o jogo não está rodando (menu, pause, game over), desliga o som
    noTone(BUZZER_PIN);
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

// Retorna o índice físico do LED com base na pista (0 a 3) e no passo lógico
// pista 0: Verde, pista 1: Vermelha, pista 2: Amarela, pista 3: Azul
// Para pista 0, passo vai de 0 a 18 (19 leds). Para as demais, de 0 a 19 (20 leds).
int obterIndiceLED(int pista, int passo) {
  if (pista == 0) {
    // Pista 0 (Verde): 19 LEDs, zig-zag invertido (índices 18 a 0)
    return 18 - passo;
  } else if (pista == 1) {
    // Pista 1 (Vermelha): 20 LEDs, direção normal (índices 19 a 38)
    return 19 + passo;
  } else if (pista == 2) {
    // Pista 2 (Amarela): 20 LEDs, zig-zag invertido (índices 58 a 39)
    return 58 - passo;
  } else if (pista == 3) {
    // Pista 3 (Azul): 20 LEDs, direção normal (índices 59 a 78)
    return 59 + passo;
  }
  return 0;
}

// Desenha a linha de mira visual fraca para orientar o jogador
void desenharCenario() {
  for (int pista = 0; pista < NUM_PISTAS; pista++) {
    int passoMira = (pista == 0) ? 16 : 17;
    int idxMira = obterIndiceLED(pista, passoMira);
    if (leds[idxMira] == CRGB::Black) {
      leds[idxMira] = CRGB(15, 15, 15); // Linha cinza fraca
    }
  }
}

// Move as notas nas pistas ativas
void moverNotas() {
  // 1. Detectar notas que chegaram ao fim da pista e passaram sem clique (miss)
  for (int pista = 0; pista < NUM_PISTAS; pista++) {
    int passoFim = (pista == 0) ? 18 : 19;
    int idxFim = obterIndiceLED(pista, passoFim);
    CRGB corNota = leds[idxFim];
    if ((pista == 0 && corNota == CRGB::Green) ||
        (pista == 1 && corNota == CRGB::Red) ||
        (pista == 2 && corNota == CRGB::Yellow) ||
        (pista == 3 && corNota == CRGB::Blue)) {
      perdeuNota();
    }
  }

  // Se o jogo acabou por falta de vidas na verificação anterior, interrompe
  if (estadoAtual == ESTADO_GAME_OVER) return;

  // 2. Desloca as notas um passo para a frente
  for (int pista = 0; pista < NUM_PISTAS; pista++) {
    int passoFim = (pista == 0) ? 18 : 19;
    for (int passo = passoFim; passo > 0; passo--) {
      int idxAtual = obterIndiceLED(pista, passo);
      int idxAnterior = obterIndiceLED(pista, passo - 1);
      leds[idxAtual] = leds[idxAnterior];
    }
    
    // 3. Gera nota no início da pista (passo 0) aleatoriamente
    int idxInicio = obterIndiceLED(pista, 0);
    if (random(0, 100) < chanceDeNota) {
      if (pista == 0) leds[idxInicio] = CRGB::Green;
      else if (pista == 1) leds[idxInicio] = CRGB::Red;
      else if (pista == 2) leds[idxInicio] = CRGB::Yellow;
      else if (pista == 3) leds[idxInicio] = CRGB::Blue;
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
  bool lerGreen = digitalRead(BTN_GREEN);
  bool lerYellow = digitalRead(BTN_YELLOW);
  bool lerRed = digitalRead(BTN_RED);
  bool lerBlue = digitalRead(BTN_BLUE);

  // Variáveis para indicar clique (físico ou serial)
  bool clickGreen = (lerGreen == LOW && antGreen == HIGH) || clickGreenSerial;
  bool clickYellow = (lerYellow == LOW && antYellow == HIGH) || clickYellowSerial;
  bool clickRed = (lerRed == LOW && antRed == HIGH) || clickRedSerial;
  bool clickBlue = (lerBlue == LOW && antBlue == HIGH) || clickBlueSerial;

  // Consome/limpa as flags seriais
  clickGreenSerial = false;
  clickYellowSerial = false;
  clickRedSerial = false;
  clickBlueSerial = false;

  // Pista 0: Verde (Green) - Zonas: 18 (Perfeito), 17 (Ótimo), 16 (Bom)
  if (clickGreen) {
    int idx18 = obterIndiceLED(0, 18);
    int idx17 = obterIndiceLED(0, 17);
    int idx16 = obterIndiceLED(0, 16);
    if (leds[idx18] == CRGB::Green) {
      acertouNota(0, 18);
    } else if (leds[idx17] == CRGB::Green) {
      acertouNota(0, 17);
    } else if (leds[idx16] == CRGB::Green) {
      acertouNota(0, 16);
    } else {
      errouNota(0);
    }
  }

  // Pista 1: Vermelha (Red) - Zonas: 19 (Perfeito), 18 (Ótimo), 17 (Bom)
  if (clickRed && NUM_PISTAS > 1) {
    int idx19 = obterIndiceLED(1, 19);
    int idx18 = obterIndiceLED(1, 18);
    int idx17 = obterIndiceLED(1, 17);
    if (leds[idx19] == CRGB::Red) {
      acertouNota(1, 19);
    } else if (leds[idx18] == CRGB::Red) {
      acertouNota(1, 18);
    } else if (leds[idx17] == CRGB::Red) {
      acertouNota(1, 17);
    } else {
      errouNota(1);
    }
  }

  // Pista 2: Amarela (Yellow) - Zonas: 19 (Perfeito), 18 (Ótimo), 17 (Bom)
  if (clickYellow && NUM_PISTAS > 2) {
    int idx19 = obterIndiceLED(2, 19);
    int idx18 = obterIndiceLED(2, 18);
    int idx17 = obterIndiceLED(2, 17);
    if (leds[idx19] == CRGB::Yellow) {
      acertouNota(2, 19);
    } else if (leds[idx18] == CRGB::Yellow) {
      acertouNota(2, 18);
    } else if (leds[idx17] == CRGB::Yellow) {
      acertouNota(2, 17);
    } else {
      errouNota(2);
    }
  }

  // Pista 3: Azul (Blue) - Zonas: 19 (Perfeito), 18 (Ótimo), 17 (Bom)
  if (clickBlue && NUM_PISTAS > 3) {
    int idx19 = obterIndiceLED(3, 19);
    int idx18 = obterIndiceLED(3, 18);
    int idx17 = obterIndiceLED(3, 17);
    if (leds[idx19] == CRGB::Blue) {
      acertouNota(3, 19);
    } else if (leds[idx18] == CRGB::Blue) {
      acertouNota(3, 18);
    } else if (leds[idx17] == CRGB::Blue) {
      acertouNota(3, 17);
    } else {
      errouNota(3);
    }
  }

  antGreen = lerGreen;
  antYellow = lerYellow;
  antRed = lerRed;
  antBlue = lerBlue;
}

// Ações quando o jogador acerta o timing da nota
void acertouNota(int pista, int passoAcerto) {
  int passoFim = (pista == 0) ? 18 : 19;
  int diferenca = passoFim - passoAcerto; // 0 = no último, 1 = no penúltimo, 2 = no antepenúltimo
  
  int pontosGanhos = 0;
  int somFrequencia = 700;
  if (diferenca == 0) {
    pontosGanhos = 15;      // Perfeito (último LED)
    somFrequencia = 1000;   // Som mais agudo e forte
    ultimoAcerto = "PERF";
  } else if (diferenca == 1) {
    pontosGanhos = 10;      // Ótimo (penúltimo LED)
    somFrequencia = 880;
    ultimoAcerto = "GREAT";
  } else {
    pontosGanhos = 5;       // Bom (antepenúltimo LED)
    somFrequencia = 700;
    ultimoAcerto = "GOOD";
  }
  
  pontuacao += pontosGanhos;
  
  // Limpa a nota acertada substituindo por um brilho branco no local exato do acerto
  int idxHit = obterIndiceLED(pista, passoAcerto);
  leds[idxHit] = CRGB::White;
  FastLED.show();
  
  // Logo após o show, reseta para preto para que a nota não persista
  leds[idxHit] = CRGB::Black;
  
  // Interrompe a música de fundo para tocar o som correspondente à qualidade do acerto
  emInterrupcaoSom = true;
  tempoFimInterrupcao = millis() + 150;
  tone(BUZZER_PIN, somFrequencia);
  
  atualizarVelocidade();
  atualizarPlacar();
}

// Ações quando o jogador clica sem ter nota na zona de acerto (erro)
void errouNota(int pista) {
  if (vidas > 0) vidas--;

  // Sinalização visual de erro na pista (pisca vermelho)
  int passoFim = (pista == 0) ? 18 : 19;
  int idxFim = obterIndiceLED(pista, passoFim);
  int idxFimMenos1 = obterIndiceLED(pista, passoFim - 1);
  leds[idxFim] = CRGB(100, 0, 0); 
  leds[idxFimMenos1] = CRGB(100, 0, 0);
  FastLED.show();

  // Interrompe para tocar som de erro grave por 150ms
  emInterrupcaoSom = true;
  tempoFimInterrupcao = millis() + 150;
  tone(BUZZER_PIN, 150);
  ultimoAcerto = "MISS";
  
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
  if (vidas > 0) vidas--;

  // Interrompe para tocar som discreto de nota perdida por 50ms
  emInterrupcaoSom = true;
  tempoFimInterrupcao = millis() + 50;
  tone(BUZZER_PIN, 100);
  ultimoAcerto = "MISS";

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
  bool clickStartFisico = false;
  if (lerStart == LOW && antStart == HIGH) {
    delay(30); // Debounce físico (aguarda ruído estabilizar)
    if (digitalRead(BTN_START) == LOW) {
      clickStartFisico = true;
    }
  }
  bool clickStart = clickStartFisico || clickStartSerial;
  clickStartSerial = false; // Consome flag serial

  if (clickStart) {
    
    if (estadoAtual == ESTADO_MENU) {
      // Começa novo jogo
      pontuacao = 0;
      vidas = 5;
      velocidade = 300;
      chanceDeNota = 5;
      ultimoAcerto = "";
      estadoAtual = ESTADO_JOGANDO;
      notaAtualMelodia = 0;
      tempoInicioNota = millis();
      duracaoNotaAtual = 0;
      emInterrupcaoSom = false;
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
  lcd.print("   GUITAR LED   ");
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
    lcd.print(ultimoAcerto);
    
    lcd.setCursor(11, 1);
    lcd.print("Lv.");
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

// Altera a velocidade e a frequência de descida das notas com base na pontuação
void atualizarVelocidade() {
  if (pontuacao < 100) {
    velocidade = 300;
    chanceDeNota = 5;
  } else if (pontuacao < 200) {
    velocidade = 260;
    chanceDeNota = 7;
  } else if (pontuacao < 400) {
    velocidade = 220;
    chanceDeNota = 9;
  } else if (pontuacao < 600) {
    velocidade = 180;
    chanceDeNota = 11;
  } else {
    velocidade = 140;
    chanceDeNota = 13;
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

// Processa a reprodução sem travamento (non-blocking) da melodia de fundo
void processarMusicaDeFundo() {
  // Se houver uma interrupção ativa por efeitos sonoros (acerto, erro ou perda)
  if (emInterrupcaoSom) {
    if (millis() >= tempoFimInterrupcao) {
      emInterrupcaoSom = false;
      // Força reiniciar o tempo para a próxima nota da melodia rodar imediatamente
      tempoInicioNota = 0; 
    } else {
      // Continua tocando o som de efeito especial (não avança a música de fundo)
      return;
    }
  }

  // Toca a melodia de fundo no tempo adequado
  unsigned long agora = millis();
  if (agora - tempoInicioNota >= duracaoNotaAtual) {
    // Avança para a próxima nota
    notaAtualMelodia = (notaAtualMelodia + 1) % NUM_NOTAS_MELODIA;
    int frequencia = melodiaNotas[notaAtualMelodia];
    duracaoNotaAtual = melodiaTempos[notaAtualMelodia];
    tempoInicioNota = agora;

    if (frequencia > 0) {
      tone(BUZZER_PIN, frequencia);
    } else {
      noTone(BUZZER_PIN);
    }
  }
}

// Lê os comandos vindos da Ponte Serial e atualiza as flags de clique correspondentes
void lerSerial() {
  while (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'G') {
      clickGreenSerial = true;
    } else if (cmd == 'Y') {
      clickYellowSerial = true;
    } else if (cmd == 'R') {
      clickRedSerial = true;
    } else if (cmd == 'B') {
      clickBlueSerial = true;
    } else if (cmd == 'S') {
      // Trava de segurança contra ruídos na porta serial:
      // Espera até 10ms pelo caractere de confirmação 'T'
      unsigned long inicioEspera = millis();
      while (Serial.available() == 0 && millis() - inicioEspera < 10) {
        // Aguarda
      }
      if (Serial.available() > 0) {
        char cmd2 = Serial.read();
        if (cmd2 == 'T') {
          clickStartSerial = true;
        }
      }
    }
  }
}