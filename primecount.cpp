#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>

// ==========================================
// PARTE B: Lógica de Primalidade e Worker
// ==========================================

// Teste de primalidade básico (CPU-bound)
bool is_prime_basic(int n) {
    if (n < 2) return false;
    if (n == 2) return true;
    if (n % 2 == 0) return false;
    
    // Testa divisores ímpares de 3 até sqrt(n)
    for (int i = 3; i * i <= n; i += 2) {
        if (n % i == 0) return false;
    }
    return true;
}

// Função que realiza a contagem num intervalo específico [start, end]
// Usada tanto pelo modo sequencial quanto pelos workers do modo concorrente
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

long long run_sequential(int N) {
    return count_primes_interval(2, N);
}

// ==========================================
// PARTE C: Implementação CONCORRENTE
// ==========================================

long long run_concurrent(int N, int P, const std::string& ipc_type) {
    // 1. Definição dos Intervalos
    // O intervalo é [2, N]. Total de números = N - 1.
    int total_nums = N - 1;
    int base_chunk = total_nums / P;
    int remainder = total_nums % P;

    // Estruturas de IPC
    int** pipe_fds = nullptr;      // Para Pipe
    long long* shm_ptr = nullptr;  // Para Shared Memory

    // 2. Setup do IPC
    if (ipc_type == "pipe") {
        pipe_fds = new int*[P];
        for (int i = 0; i < P; i++) {
            pipe_fds[i] = new int[2];
            if (pipe(pipe_fds[i]) == -1) {
                perror("Erro ao criar pipe");
                exit(1);
            }
        }
    } else if (ipc_type == "shm") {
        // Mapeia memória anônima compartilhada (MAP_SHARED | MAP_ANONYMOUS)
        size_t shm_size = sizeof(long long) * P;
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

    // 3. Loop de criação de processos (Fork)
    int current_start = 2;

    for (int i = 0; i < P; i++) {
        // Calcula limites do intervalo para o processo i
        int chunk_size = base_chunk + (i < remainder ? 1 : 0);
        int current_end = current_start + chunk_size - 1;

        pid_t pid = fork();

        if (pid < 0) {
            perror("Erro no fork");
            exit(1);
        } 
        else if (pid == 0) {
            // --- PROCESSO FILHO (WORKER) ---
            
            // Gerenciamento de descritores de Pipe no filho
            if (ipc_type == "pipe") {
                for (int j = 0; j < P; j++) {
                    close(pipe_fds[j][0]); // Filho nunca lê
                    if (j != i) close(pipe_fds[j][1]); // Fecha escrita dos outros pipes
                }
            }

            // Executa a tarefa
            long long primes_found = count_primes_interval(current_start, current_end);

            // Envia resultado
            if (ipc_type == "pipe") {
                if (write(pipe_fds[i][1], &primes_found, sizeof(long long)) == -1) {
    perror("Erro na escrita do pipe");
    exit(1);
}
                close(pipe_fds[i][1]); // Fecha e envia EOF
            } else if (ipc_type == "shm") {
                shm_ptr[i] = primes_found; // Escreve no índice reservado
            }
            
            // Limpeza de memória do heap herdada (boa prática)
            if (ipc_type == "pipe") {
                for(int k=0; k<P; k++) delete[] pipe_fds[k];
                delete[] pipe_fds;
            }

            exit(0); // Filho encerra aqui
        }

        // --- PROCESSO PAI (MASTER) ---
        
        // Se usar pipe, pai fecha a ponta de escrita deste pipe
        if (ipc_type == "pipe") {
            close(pipe_fds[i][1]);
        }

        // Prepara start para o próximo worker
        current_start = current_end + 1;
    }

    // 4. Coleta de Resultados e Wait
    long long total_primes = 0;

    // Primeiro: Wait for all (evita zumbis)
    for (int i = 0; i < P; i++) {
        wait(NULL);
    }

    // Segundo: Agrega resultados
    if (ipc_type == "pipe") {
        for (int i = 0; i < P; i++) {
            long long temp = 0;
            if (read(pipe_fds[i][0], &temp, sizeof(long long)) > 0) {
                total_primes += temp;
            }
            close(pipe_fds[i][0]);
            delete[] pipe_fds[i];
        }
        delete[] pipe_fds;
    } else if (ipc_type == "shm") {
        for (int i = 0; i < P; i++) {
            total_primes += shm_ptr[i];
        }
        munmap(shm_ptr, sizeof(long long) * P);
    }

    return total_primes;
}

// ==========================================
// PARTE A: Main e Validação
// ==========================================

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
    // Validação básica de quantidade de argumentos
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];
    int N = 0;
    
    // Validação de N
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

    int next_arg_idx = 3;

    // Lógica para modo Concorrente
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

    // Busca argumento opcional --algo
    for (int i = next_arg_idx; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--algo" && i + 1 < argc) {
            algo = argv[i + 1];
            i++;
        }
    }

    // Medição de Tempo e Execução
    auto start_time = std::chrono::steady_clock::now();
    
    long long primes = 0;
    if (mode == "seq") {
        primes = run_sequential(N);
    } else {
        primes = run_concurrent(N, P, ipc);
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // Saída Formatada
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