all: ls

ls: monitor_neighbors.cpp monitor_neighbors.h
	g++ -pthread -o ls_router monitor_neighbors.cpp monitor_neighbors.h

.PHONY: clean
clean:
	rm *.o ls_router
