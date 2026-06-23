import pygame
import serial
import time
import sys

# ==============================================================================
# CONFIGURAÇÃO DA PONTE SERIAL
# ==============================================================================
# Defina a porta COM correspondente ao seu ESP32-S3 no computador.
# Exemplos: "COM3", "COM4" no Windows ou "/dev/ttyACM0", "/dev/ttyUSB0" no Linux/macOS.
PORTA_SERIAL = "COM3"
BAUD_RATE = 115200

# Mapeamento dos botões da guitarra para os caracteres correspondentes.
# Se o seu adaptador mapear os botões físicos (Vermelho, Verde, Azul) para outros
# índices no Pygame, altere estes valores.
# DICA: Pressione os botões no terminal para ver os logs com os IDs reais!
BOTAO_VERDE_IDX = 0      # Geralmente botão 0 ou 2 (Verde -> 'G')
BOTAO_VERMELHO_IDX = 1   # Geralmente botão 1 ou 3 (Vermelho -> 'R')
BOTAO_AZUL_IDX = 2       # Geralmente botão 2 ou 0 (Azul -> 'B')
BOTAO_AMARELO_IDX = 3    # Geralmente botão 3 (Amarelo -> 'Y')
# ==============================================================================

# Inicialização do Pygame e subsistema de Joystick
pygame.init()
pygame.joystick.init()

def conectar_serial(porta, baud):
    """
    Estabelece conexão com a porta serial. Retorna o objeto da conexão ou None se falhar.
    """
    try:
        conexao = serial.Serial(porta, baud, write_timeout=0.1)
        # Limpa buffers de entrada/saída
        conexao.reset_input_buffer()
        conexao.reset_output_buffer()
        print(f"\n[SERIAL] Conectado com sucesso na porta {porta}!")
        return conexao
    except (serial.SerialException, FileNotFoundError) as e:
        print(f"[SERIAL] Aguardando conexão na porta {porta}... ({e})", end="\r")
        time.sleep(1)
        return None

def main():
    print("=========================================================")
    print("   PONTE SERIAL GUITARRA DE PS2 -> ESP32-S3 (GUITAR HERO)   ")
    print("=========================================================")
    
    clock = pygame.time.Clock()
    conexao_serial = None
    joystick_detectado = False
    joystick = None

    rodando = True
    while rodando:
        # 1. Gerenciamento do Joystick (Conexão e Inicialização)
        if not joystick_detectado:
            joystick_count = pygame.joystick.get_count()
            if joystick_count > 0:
                try:
                    joystick = pygame.joystick.Joystick(0)
                    joystick.init()
                    print(f"\n[JOYSTICK] Guitarra/Controle detectado: {joystick.get_name()}")
                    joystick_detectado = True
                except pygame.error as e:
                    print(f"\n[JOYSTICK] Erro ao inicializar controle: {e}")
                    time.sleep(1)
            else:
                print("[JOYSTICK] Aguardando conexão da guitarra via adaptador USB...", end="\r")
                time.sleep(1)
                # Reinicializa subsistema para detectar novas conexões
                pygame.joystick.quit()
                pygame.joystick.init()
                continue

        # 2. Gerenciamento da Conexão Serial (Robusto a desconexões)
        if conexao_serial is None:
            conexao_serial = conectar_serial(PORTA_SERIAL, BAUD_RATE)
            if conexao_serial is None:
                # Processa a fila de eventos do Pygame para a aplicação não travar
                for event in pygame.event.get():
                    if event.type == pygame.QUIT:
                        rodando = False
                continue

        # 3. Loop principal de leitura de eventos
        try:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    rodando = False

                # Detecta evento de clique do botão (Borda de Descida / Button Down)
                elif event.type == pygame.JOYBUTTONDOWN:
                    btn_id = event.button
                    char_enviar = None
                    cor_nome = ""

                    if btn_id == BOTAO_VERMELHO_IDX:
                        char_enviar = 'R'
                        cor_nome = "VERMELHO (Red)"
                    elif btn_id == BOTAO_VERDE_IDX:
                        char_enviar = 'G'
                        cor_nome = "VERDE (Green)"
                    elif btn_id == BOTAO_AZUL_IDX:
                        char_enviar = 'B'
                        cor_nome = "AZUL (Blue)"
                    elif btn_id == BOTAO_AMARELO_IDX:
                        char_enviar = 'Y'
                        cor_nome = "AMARELO (Yellow)"

                    # Envio e Log
                    if char_enviar:
                        print(f"[CLIQUE] Botão ID {btn_id} -> {cor_nome} | Enviando '{char_enviar}'")
                        try:
                            conexao_serial.write(char_enviar.encode('utf-8'))
                            conexao_serial.flush()  # Envia imediatamente (baixa latência)
                        except (serial.SerialException, AttributeError) as e:
                            print(f"\n[SERIAL] Erro ao transmitir dados: {e}")
                            print("[SERIAL] Fechando conexão inválida...")
                            try:
                                conexao_serial.close()
                            except:
                                pass
                            conexao_serial = None
                    else:
                        # Log informativo para ajudar o usuário a descobrir o ID dos demais botões
                        print(f"[INFO] Outro botão pressionado: ID {btn_id}")

            # Limita a taxa de atualização para baixo uso de CPU
            # 200 Hz oferece precisão de até 5ms de polling, garantindo baixíssima latência
            clock.tick(200)

        except pygame.error as e:
            print(f"\n[JOYSTICK] Controle desconectado ou com erro: {e}")
            joystick_detectado = False
            time.sleep(1)

    # Encerramento limpo
    if conexao_serial:
        conexao_serial.close()
    pygame.quit()
    print("\nPonte Serial encerrada.")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nInterrompido via teclado pelo usuário.")
        pygame.quit()
        sys.exit(0)
