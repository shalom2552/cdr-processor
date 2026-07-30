GCC = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinc

TEST_SRC = $(wildcard tests/*.cpp) 
TEST_BIN = build/tests

.PHONY: test
test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $(TEST_BIN)

clean:
	rm -rf build

