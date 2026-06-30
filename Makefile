all:
	g++ -std=c++17 -I./emulator emulator/main.cpp -o os_emulator

clean:
	rm -f os_emulator
