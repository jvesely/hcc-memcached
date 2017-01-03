SRCS= \
	main.cpp \
	process-cpu.cpp \
	process-gpu.cpp

BIN=memcached

OBJS=$(SRCS:.cpp=.o)
DEPS=$(OBJS:.o=.d)

HCC_CONFIG=/opt/hcc-amdgpu/bin/hcc-config
CXX=/opt/hcc-amdgpu/bin/hcc

#hcc-config mixes compiler and preprocessor flags
CPPFLAGS=$(shell $(HCC_CONFIG) --cxxflags --install) -Wall
CXXFLAGS=$(shell $(HCC_CONFIG) --cxxflags --install) -g -O3 -Wall
LDFLAGS=$(shell $(HCC_CONFIG) --ldflags --install)

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c $< $(CPPFLAGS) $(CXXFLAGS) -o $@

%.d: %.cpp
	$(CXX) -MMD -MF $@ $(CPPFLAGS) $< -E > /dev/null

-include $(DEPS)

clean:
	rm -vf $(OBJS) $(BINS) $(DEPS)
