FROM python:3.7-slim
RUN apt update
RUN apt install -y iproute2
ENV LD_LIBRARY_PATH=/xdp
CMD /bin/bash