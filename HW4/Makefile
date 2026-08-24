CXX = g++
CXXFLAGS = -std=c++14 -Wall
LDFLAGS = -lpthread

all: socks_server pj4.cgi

socks_server: socks_server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

pj4.cgi: console.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f socks_server pj4.cgi