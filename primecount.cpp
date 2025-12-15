/**
 * =========================================================================================
 * INCLUSÃO DE BIBLIOTECAS
 * =========================================================================================
 */
#include <iostream>     // Para entrada e saída padrão (cout, cerr)
#include <vector>       // Para uso de vetores dinâmicos (std::vector)
#include <string>       // Para manipulação de strings (std::string)
#include <cmath>        // Para funções matemáticas (sqrt)
#include <cstring>      // Para manipulação de strings estilo C
#include <chrono>       // Para medição de tempo de alta precisão
#include <unistd.h>     // Para chamadas de sistema POSIX (fork, pipe, write, read, close)
#include <sys/wait.h>   // Para a chamada de sistema wait()
#include <sys/mman.h>   // Para gerenciamento de memória (mmap, munmap) para Shared Memory

// ==========================================
// PARTE B: Lógica de Primalidade e Worker
// ==========================================

/**
 * Função: is_prime_basic
 * ----------------------
 * Verifica se um número inteiro 'n' é primo.
 * Algoritmo: Divisão por tentativas (Trial Division).
 * Complexidade: O(sqrt(n)) - Método "CPU-bound" ideal para testar processamento.
 */
bool is_prime_basic(int n) {
    // Casos base: números menores que 2 não são primos
    if (n < 2) return false;
    // 2 é o único número par primo
    if (n == 2) return true;
    // Elimina todos os outros pares rapidamente
    if (n % 2 == 0) return false;
    
    // Otimização: Testa apenas divisores ímpares de 3 até a raiz quadrada de n.
    // Se n tiver um fator maior que sqrt(n), o outro fator necessariamente é menor que sqrt(n).
    for (int i = 3; i * i <= n; i += 2) {
        if (n % i == 0) return false;
    }
    return true;
}

/**
 * Função: count_primes_interval
 * -----------------------------
 * Conta quantos números primos existem em um intervalo fechado [start, end].
 * Esta é a "tarefa" que será executada tanto pelo modo sequencial quanto
 * pelos processos filhos (workers) no modo paralelo.
 */
long long count_primes_interval(int start, int end) {
    long long count = 0;
    for (int i = start; i <= end; i++) {
        if (is_prime_basic(i)) {
            count++;
        }
    }
    return count;
}

// ==========================================
// Implementação SEQUENCIAL
// ==========================================

/**
 * Função: run_sequential
 * ----------------------
 * Executa a contagem de primos em um único processo (thread principal).
 * Serve como base de comparação (baseline) para calcular o Speedup.
 */
long long run_sequential(int N) {
    // Simplesmente chama a função de contagem do início ao fim
    return count_primes_interval(2, N);
}

// ==========================================
// PARTE C: Implementação CONCORRENTE
// ==========================================

/**
 * Função: run_concurrent
 * ----------------------
 * Gerencia a execução paralela utilizando múltiplos processos.
 * * Passos principais:
 * 1. Divide o intervalo total em fatias para cada processo.
 * 2. Prepara o canal de comunicação (Pipe ou Shared Memory).
 * 3. Cria processos filhos (fork).
 * 4. Filhos processam e enviam resultados.
 * 5. Pai espera filhos e agrega resultados.
 */
long long run_concurrent(int N, int P, const std::string& ipc_type) {
    // ---------------------------------------------------------
    // 1. Definição dos Intervalos (Balanceamento de Carga)
    // ---------------------------------------------------------
    // O intervalo de interesse é [2, N].
    int total_nums = N - 1;       // Quantidade total de números a verificar
    int base_chunk = total_nums / P; // Tamanho base da fatia para cada processo
    int remainder = total_nums % P;  // Resto da divisão (para distribuir o excedente)

    // ---------------------------------------------------------
    // 2. Setup do IPC (Inter-Process Communication)
    // ---------------------------------------------------------
    int** pipe_fds = nullptr;      // Matriz para armazenar descritores de arquivo (se usar Pipe)
    long long* shm_ptr = nullptr;  // Ponteiro para a memória compartilhada (se usar SHM)

    if (ipc_type == "pipe") {
        // Aloca array de ponteiros para os pipes
        pipe_fds = new int*[P];
        for (int i = 0; i < P; i++) {
            pipe_fds[i] = new int[2]; // Cada pipe tem 2 descritores: [0]=leitura, [1]=escrita
            // Cria o pipe. Se falhar, encerra.
            if (pipe(pipe_fds[i]) == -1) {
                perror("Erro ao criar pipe");
                exit(1);
            }
        }
    } else if (ipc_type == "shm") {
        // Configura Memória Compartilhada usando mmap.
        // MAP_SHARED: As alterações são visíveis para outros processos mapeando a mesma região.
        // MAP_ANONYMOUS: A memória não é baseada em arquivo, é criada na RAM e zerada.
        // PROT_READ | PROT_WRITE: Permissão de leitura e escrita.
        size_t shm_size = sizeof(long long) * P; // Espaço para um contador (long long) por processo
        shm_ptr = (long long*)mmap(NULL, shm_size, PROT_READ | PROT_WRITE, 
                                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        
        if (shm_ptr == MAP_FAILED) {
            perror("Erro no mmap");
            exit(1);
        }
    } else {
        std::cerr << "IPC invalido (use 'pipe' ou 'shm')" << std::endl;
        exit(1);
    }

    // ---------------------------------------------------------
    // 3. Loop de Criação de Processos (Fork)
    // ---------------------------------------------------------
    int current_start = 2; // O primeiro número a verificar é 2

    for (int i = 0; i < P; i++) {
        // Calcula onde começa e termina o intervalo do processo 'i'
        // Distribui o 'remainder' dando 1 número extra para os primeiros processos
        int chunk_size = base_chunk + (i < remainder ? 1 : 0);
        int current_end = current_start + chunk_size - 1;

        // Cria um novo processo duplicando o atual
        pid_t pid = fork();

        if (pid < 0) {
            perror("Erro no fork");
            exit(1);
        } 
        else if (pid == 0) {
            // =====================================================
            // CÓDIGO DO PROCESSO FILHO (WORKER)
            // =====================================================
            
            // Gerenciamento de Pipes no Filho
            if (ipc_type == "pipe") {
                for (int j = 0; j < P; j++) {
                    close(pipe_fds[j][0]); // Filho NUNCA lê do pipe, fecha leitura
                    if (j != i) close(pipe_fds[j][1]); // Fecha escrita dos pipes dos IRMÃOS
                }
            }

            // Realiza o trabalho pesado (CPU-bound)
            long long primes_found = count_primes_interval(current_start, current_end);

            // Envia o resultado para o Pai
            if (ipc_type == "pipe") {
                // Escreve o resultado binário no pipe dedicado a este processo (i)
                if (write(pipe_fds[i][1], &primes_found, sizeof(long long)) == -1) {
                    perror("Erro na escrita do pipe");
                    exit(1);
                }
                close(pipe_fds[i][1]); // Fecha a ponta de escrita após enviar (envia EOF)
            } else if (ipc_type == "shm") {
                // Escreve diretamente no slot do array compartilhado na memória
                shm_ptr[i] = primes_found; 
            }
            
            // Limpeza de memória alocada no heap (herdada do pai) para evitar vazamento no valgrind
            if (ipc_type == "pipe") {
                for(int k=0; k<P; k++) delete[] pipe_fds[k];
                delete[] pipe_fds;
            }

            exit(0); // OBRIGATÓRIO: Filho termina aqui para não continuar o loop do pai
        }

        // =====================================================
        // CÓDIGO DO PROCESSO PAI (MASTER) CONTINUAÇÃO DO LOOP
        // =====================================================
        
        // Se usar pipe, pai fecha a ponta de ESCRITA deste pipe específico.
        // Se o pai mantiver aberto, o 'read' nunca retornará 0 (EOF) se decidirmos ler em loop.
        if (ipc_type == "pipe") {
            close(pipe_fds[i][1]);
        }

        // Atualiza o início para o próximo worker
        current_start = current_end + 1;
    }

    // ---------------------------------------------------------
    // 4. Sincronização e Agregação de Resultados
    // ---------------------------------------------------------
    long long total_primes = 0;

    // Passo A: Esperar TODOS os filhos terminarem.
    // Isso é crucial para evitar processos "zumbis" e garantir que todos calcularam.
    for (int i = 0; i < P; i++) {
        wait(NULL);
    }

    // Passo B: Coletar e somar os resultados parciais
    if (ipc_type == "pipe") {
        for (int i = 0; i < P; i++) {
            long long temp = 0;
            // Lê do pipe. O read retorna > 0 se leu bytes.
            if (read(pipe_fds[i][0], &temp, sizeof(long long)) > 0) {
                total_primes += temp;
            }
            close(pipe_fds[i][0]); // Fecha a ponta de leitura
            delete[] pipe_fds[i];  // Libera memória do par de inteiros
        }
        delete[] pipe_fds; // Libera o array de ponteiros
    } else if (ipc_type == "shm") {
        // No caso de memória compartilhada, os dados já estão lá
        for (int i = 0; i < P; i++) {
            total_primes += shm_ptr[i];
        }
        // Desfaz o mapeamento da memória (limpeza)
        munmap(shm_ptr, sizeof(long long) * P);
    }

    return total_primes;
}

// ==========================================
// PARTE A: Main e Validação
// ==========================================

// Função auxiliar para mostrar como usar o programa caso falte argumentos
void print_usage(const char* prog_name) {
    std::cerr << "Uso:\n"
              << "  Sequencial: " << prog_name << " seq <N> [--algo basic]\n"
              << "  Paralelo:   " << prog_name << " par <N> <P> <IPC> [--algo basic]\n\n"
              << "Argumentos:\n"
              << "  N:    Inteiro >= 2\n"
              << "  P:    Inteiro >= 1\n"
              << "  IPC:  'pipe' ou 'shm'\n";
}

int main(int argc, char* argv[]) {
    // Validação mínima de quantidade de argumentos
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    // Leitura dos argumentos básicos
    std::string mode = argv[1];
    int N = 0;
    
    // Converte e valida N
    try {
        N = std::stoi(argv[2]);
    } catch (...) {
        std::cerr << "Erro: N deve ser inteiro." << std::endl;
        return 1;
    }
    if (N < 2) {
        std::cerr << "Erro: N deve ser >= 2." << std::endl;
        return 1;
    }

    int P = 0;
    std::string ipc = "none";
    std::string algo = "basic"; 

    int next_arg_idx = 3; // Índice para continuar a leitura de argumentos

    // Validações específicas para o modo Paralelo
    if (mode == "par") {
        if (argc < 5) {
            std::cerr << "Erro: Modo 'par' requer P e IPC." << std::endl;
            print_usage(argv[0]);
            return 1;
        }
        
        try {
            P = std::stoi(argv[3]);
        } catch (...) {
            std::cerr << "Erro: P deve ser inteiro." << std::endl;
            return 1;
        }
        if (P < 1) {
            std::cerr << "Erro: P deve ser >= 1." << std::endl;
            return 1;
        }

        ipc = argv[4];
        if (ipc != "pipe" && ipc != "shm") {
            std::cerr << "Erro: IPC deve ser 'pipe' ou 'shm'." << std::endl;
            return 1;
        }
        
        next_arg_idx = 5;
    } else if (mode != "seq") {
        std::cerr << "Erro: Modo desconhecido (use 'seq' ou 'par')." << std::endl;
        return 1;
    }

    // Busca argumento opcional --algo (se existir)
    for (int i = next_arg_idx; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--algo" && i + 1 < argc) {
            algo = argv[i + 1];
            i++;
        }
    }

    // ---------------------------------------------------------
    // Execução e Medição de Tempo
    // ---------------------------------------------------------
    
    // Captura o tempo inicial (Monotônico, imune a mudanças de horário do sistema)
    auto start_time = std::chrono::steady_clock::now();
    
    long long primes = 0;
    
    // Decide qual função executar com base no modo
    if (mode == "seq") {
        primes = run_sequential(N);
    } else {
        primes = run_concurrent(N, P, ipc);
    }

    // Captura tempo final e calcula a diferença em milissegundos
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // ---------------------------------------------------------
    // Saída Formatada (conforme requisitos do trabalho)
    // ---------------------------------------------------------
    std::cout << "mode=" << mode 
              << " N=" << N;
    
    if (mode == "par") {
        std::cout << " P=" << P 
                  << " ipc=" << ipc;
    }
    
    std::cout << " primes=" << primes 
              << " time_ms=" << elapsed_ms 
              << std::endl;

    return 0;
}