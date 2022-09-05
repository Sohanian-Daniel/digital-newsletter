build: server subscriber

subscriber:
	g++ subscriber.cpp helpers.cpp -o subscriber -ggdb

server:
	g++ server.cpp helpers.cpp -o server -ggdb

clean:
	rm -f subscriber
	rm -f server