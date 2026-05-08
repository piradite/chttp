.PHONY: all build test fuzz clean lint

all: build

build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_ASAN=ON
	cmake --build build

release:
	cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
	cmake --build build_release

test: build
	@echo "Running unit tests"
	cd build && ctest --output-on-failure
	@echo "Running integration tests"
	./tests/integration/test_server.sh

fuzz:
	CC=clang CXX=clang++ cmake -S . -B build_fuzz -DCMAKE_BUILD_TYPE=Debug -DBUILD_FUZZ=ON -DENABLE_ASAN=ON
	cmake --build build_fuzz
	./build_fuzz/tests/fuzz_parser -max_total_time=10

lint:
	clang-format -i $$(find src include tests -name '*.c' -o -name '*.h' -o -name '*.cpp')

clean:
	rm -rf build build_release build_fuzz build_asan
