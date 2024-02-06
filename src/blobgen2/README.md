# blobgen2 instructions

This is a guideline how to compile and run blobgen2 code.

## 1. Compile blobgen2
- Open a bash console.
- Clone budabackend repo and cd to its root.
- set ARCH environment variable to a desired architecture and execute the make command:
```
$ export ARCH=grayskull
$ make src/blobgen2
```

This will produce the `build/bin/blobgen2` executable, as well as .o, .d, and other files at `build/obj/src` directory.

## 2. Running a basic test
After a successful blobgen2 compilation, execute:
```
./src/blobgen2/test/test_blobgen2.sh
```

## 3. Cleaning blobgen2

### Cleaning output of blobgen2 tests
```
$ make clean_blobgen2_test
```

This will delete:
1. `src/blobgen2/test/inputs/*/out_blob.yaml` files.


### Cleaning all blobgen2 build and run related files
```
$ make clean_blobgen2
```

This will delete:
1. `build/bin/blogen2` executable 
2. `build/obj/src/blobgen2` directory with .o, .d, and other files
3. `tt_build/blobgen2` directory
4. Test output files through invocation of `clean_blobgen_test` make target.