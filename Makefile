SRCS= \
	main.cpp \
	memcached-protocol.cpp \
	process-cpu.cpp \
	process-hybrid.cpp \
	process-gpu.cpp

BIN=memcached

OBJS=$(SRCS:.cpp=.o)
DEPS=$(OBJS:.o=.d)

HCC_CONFIG=/opt/rocm/hcc-amdgpu/bin/hcc-config
CXX=/opt/rocm/hcc-amdgpu/bin/hcc

#hcc-config mixes compiler and preprocessor flags
CPPFLAGS=$(shell $(HCC_CONFIG) --cxxflags --install) -Wall
CXXFLAGS=$(shell $(HCC_CONFIG) --cxxflags --install) -g -O3 -Wall
#for some reason hcc does not include libm
LDFLAGS=$(shell $(HCC_CONFIG) --ldflags --install) -lm

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c $< $(CPPFLAGS) $(CXXFLAGS) -o $@

%.d: %.cpp
	$(CXX) -MMD -MF $@ $(CPPFLAGS) $< -E > /dev/null

-include $(DEPS)

clean:
	rm -vf $(OBJS) $(BIN) $(DEPS)
