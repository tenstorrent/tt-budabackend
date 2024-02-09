clang-format -i -style=file tests/eltwise_unary/*
clang-format -i -style=file tests/eltwise_binary/*
clang-format -i -style=file tests/hlkc_test/*
clang-format -i -style=file tests/matmul/*
clang-format -i -style=file tests/reduce/*
clang-format -i -style=file tests/tile_sync/*
clang-format -i -style=file tests/matmul_large/*
clang-format -i -style=file tests/matmul_large_add/*
clang-format -i -style=file tests/multi_in/*
clang-format -i -style=file tests/multi_out/*

clang-format -i -style=file tests/lib/**/*.h
clang-format -i -style=file tests/lib/**/*.cpp

clang-format -i -style=file src/*.cpp
clang-format -i -style=file inc/**/*.h
clang-format -i -style=file inc/*.h
clang-format -i -style=file *.cpp
