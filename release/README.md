# How to create a manual release

1. Cloned BBE into a folder and choose the correct commit that you want to build

```
git clone git@yyz-gitlab.local.tenstorrent.com:tenstorrent/budabackend.git
git submodule update --init --recursive
```

2. Get the base container for Pybuda 

`docker pull yyz-gitlab.local.tenstorrent.com:5005/tenstorrent/pybuda/pybuda-base:latest`

The container should be based on this Docker file:
`https://yyz-gitlab.local.tenstorrent.com/tenstorrent/pybuda/-/blob/main/ci/deployment/Dockerfile-base?ref_type=heads`

3. Run the Docker container and mount a volume with BBE code

`docker run -it --rm --shm-size=4g --device /dev/tenstorrent -v /dev/hugepages-1G:/dev/hugepages-1G  -v "$PWD:/bbe" -v "/home/software:/home/software" yyz-gitlab.local.tenstorrent.com:5005/tenstorrent/pybuda/pybuda-base:latest bash`

4. Build within the container

```
apt-get update
apt-get install -y git && git config --global --add safe.directory /bbe

cd /bbe
make clean
pip3 install pyyaml # Need to figure out why this is necessary in  `genlist_ckernel.py`
CC=gcc CXX=g++ CONFIG=release ARCH_NAME=wormhole_b0 DEVICE_VERSIM_INSTALL_ROOT=./ ROOT=./ make backend build_hw
```

5. Observe that the files that appear in build folder

Use the script that organizes all the files in the similar manner to the https://yyz-gitlab.local.tenstorrent.com/tenstorrent/pybuda/-/blob/main/setup.py?ref_type=heads 

6. Copy all of the built files into a single release folder
```
python3.8 collect_release_artifacts.py --release_folder "./collected_for_release"
zip -r collected.zip ./collected_for_release
```

7. Create an internal Gitlab release and add appropriate Release notes here:
https://yyz-gitlab.local.tenstorrent.com/tenstorrent/budabackend/-/releases

For the naming convention, it has been decided for now to follow the Pybuda with `v0.X.XX.(gs|wh_b0).YYMMDD`

8. Upload the artifact to the package registry
```
curl --header "PRIVATE-TOKEN: <token>" \
     --upload-file collected.zip \
     "https://yyz-gitlab.local.tenstorrent.com/api/v4/projects/203/packages/generic/budabackend/<use the release tag>/collected.zip"
```

9. Edit the release and associate the uploaded file with the release


# How to use Standalone BBE Release

1. Navigate to `./collected_for_release/demos` directory and run `make`
2. Observe built app in `./demos/app`, run the app command:

`LD_LIBRARY_PATH=../budabackend_lib/build/lib BUDA_HOME=../budabackend_lib/ ./app/test_standalone_runtime /home/software/spatial2/backend/binaries/CI_TTI_TEST_BINARIES_WH/bert.tti`


## Appendix - Release folder structure

The following header files should be present in `release folder/budabackend_lib/build/include` folder


```
|-- backend
|   |-- tt_backend.hpp
|   |-- tt_backend_api.hpp
|   `-- tt_backend_api_types.hpp
|-- common
|   |-- param_lib.hpp
|   |-- env_lib.hpp
|   `-- tti_lib.hpp
|-- device
|   |-- tt_arch_types.h
|   `-- tt_xy_pair.h
|-- perf_lib
|   |-- op_model
|   |   |-- op_model.hpp
|   |   |-- op_params.hpp
|   |   `-- sparse_matmul_params.hpp
`-- third_party
    `-- json
        `-- json.hpp
```