docker run -it --name xdp_manager -v xdp_vol:/xdp --ulimit memlock=-1:-1 --privileged --ipc=shareable xdp

docker run -it --name xdp_client -v xdp_vol:/xdp --ulimit memlock=-1:-1 --privileged --ipc=container:xdp_manager --network=container:xdp_manager --pid=container:xdp_manager xdp