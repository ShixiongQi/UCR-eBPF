FROM ubuntu:20.04
RUN apt update
RUN apt install -y iproute2
COPY bounce bounce_kern.o client env_setup_veth.sh libbpf.so.0 manager rx_kern.o /xdp_init/
CMD /xdp_init/env_setup_veth.sh