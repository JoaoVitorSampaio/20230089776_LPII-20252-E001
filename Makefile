# Makefile - Exercício Prático 001
# LPII - Programação Concorrente (2025.2)
# Aluno: João Vitor Sampaio
# Matrícula: 20230089776

CXX := g++
# Mudado para -O3 para maximizar performance (importante para o speedup)
CXXFLAGS := -O3 -std=c++17 -Wall -Wextra -pedantic
TARGET := primecount
SRC := primecount.cpp

.PHONY: all clean run-seq run-pipe run-shm

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

# Adicionado /usr/bin/time -v para já sair com as métricas do relatório
run-seq: $(TARGET)
	@echo "--- Executando Sequencial ---"
	/usr/bin/time -v ./$(TARGET) seq 5000000

run-pipe: $(TARGET)
	@echo "--- Executando Paralelo (PIPE) ---"
	/usr/bin/time -v ./$(TARGET) par 5000000 4 pipe

run-shm: $(TARGET)
	@echo "--- Executando Paralelo (SHM) ---"
	/usr/bin/time -v ./$(TARGET) par 5000000 4 shm

clean:
	rm -f $(TARGET)