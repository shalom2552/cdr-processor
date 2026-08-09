CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -MMD -MP -pthread -Iinc
LDFLAGS  = -pthread

BUILD    = build
CDR_GEN  = generator

OBJDIR   = $(BUILD)/obj
BIN      = $(BUILD)/main
GW_BIN   = $(BUILD)/gateway
TEST_BIN = $(BUILD)/tests

# ---- targets --------------------------------------------------------------
.PHONY: all build run query test gen debug release clean

all: $(BIN) $(GW_BIN) $(TEST_BIN)

build: $(BIN)

run: $(BIN)
	@./$(BIN)

query: $(GW_BIN)
	@./$(GW_BIN)

test: $(TEST_BIN)
	@./$(TEST_BIN)

gen:
	@python3 -m $(CDR_GEN) $(ARGS)

debug:   CXXFLAGS += -g -O0 -fsanitize=address,undefined
debug:   LDFLAGS  += -fsanitize=address,undefined
debug:   build

release: CXXFLAGS += -O2 -DNDEBUG
release: build

clean:
	rm -rf $(BUILD)

# ---- third party ----------------------------------------------------------
THIRD_PARTY = third_party
RMQ         = $(THIRD_PARTY)/rabbitmq-c
HIREDIS     = $(THIRD_PARTY)/hiredis

CXXFLAGS += -I$(THIRD_PARTY) -I$(RMQ)/include

RMQ_SRCS = $(wildcard $(RMQ)/src/*.c)
RMQ_OBJS = $(patsubst $(RMQ)/src/%.c,$(BUILD)/rmq/%.o,$(RMQ_SRCS))

HIREDIS_SRCS = $(filter-out $(HIREDIS)/dict.c,$(wildcard $(HIREDIS)/*.c))
HIREDIS_OBJS = $(patsubst $(HIREDIS)/%.c,$(BUILD)/hiredis/%.o,$(HIREDIS_SRCS))

THIRD_PARTY_OBJS = $(RMQ_OBJS) $(HIREDIS_OBJS)

$(BUILD)/rmq/%.o: $(RMQ)/src/%.c
	@mkdir -p $(@D)
	$(CC) -I$(RMQ)/include -DAMQP_STATIC -DHAVE_CONFIG_H -O2 -w -MMD -MP -c $< -o $@

$(BUILD)/hiredis/%.o: $(HIREDIS)/%.c
	@mkdir -p $(@D)
	$(CC) -I$(HIREDIS) -O2 -w -MMD -MP -c $< -o $@

# ---- app ------------------------------------------------------------------
SRC = $(shell find src -name '*.cpp')
OBJ = $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRC))

APP_OBJ = $(filter-out $(OBJDIR)/main.o $(OBJDIR)/gateway_main.o,$(OBJ))

$(BIN): $(OBJDIR)/main.o $(APP_OBJ) $(THIRD_PARTY_OBJS)
	@mkdir -p $(@D)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(GW_BIN): $(OBJDIR)/gateway_main.o $(APP_OBJ) $(THIRD_PARTY_OBJS)
	@mkdir -p $(@D)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---- tests ----------------------------------------------------------------
TEST_SRC = $(shell find tests -name '*.cpp')
TEST_OBJ = $(patsubst tests/%.cpp,$(OBJDIR)/tests/%.o,$(TEST_SRC))

$(TEST_BIN): $(TEST_OBJ) $(APP_OBJ) $(THIRD_PARTY_OBJS)
	@mkdir -p $(@D)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/tests/%.o: tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---- deps -----------------------------------------------------------------
DEPS = $(OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(THIRD_PARTY_OBJS:.o=.d)

-include $(DEPS)
