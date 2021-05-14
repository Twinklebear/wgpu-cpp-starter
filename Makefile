main.js: main.cpp
	em++ -s USE_WEBGPU=1 $^ -o $@

.PHONY: clean
clean:
	rm -f main.js main.wasm

