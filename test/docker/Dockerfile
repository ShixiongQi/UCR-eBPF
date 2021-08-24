FROM ubuntu:20.04
RUN apt update
RUN apt install -y iproute2
ENV LD_LIBRARY_PATH=/xdp
COPY bounce bounce_kern.o client env_setup_veth.sh libbpf.so.0 manager rx_kern.o /xdp/
EXPOSE 80
CMD /bin/bash